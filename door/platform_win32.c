/*
 * platform_win32.c -- the door on Windows (Mystic BBS for Windows). See platform.h.
 *
 * Mystic for Windows starts a DOOR32 door with DOOR32.SYS saying comm type 2 and, on line 2, the caller's telnet
 * socket, inherited by the door. So the door is the telnet end itself: it talks on that socket with send/recv and does
 * the telnet layer (telnet.c). No stdin, no console.
 *
 * Without a drop file (or with comm type 0) it uses the console instead, to try it out locally.
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <conio.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "telnet.h"

enum { CONN_CONSOLE, CONN_PIPE, CONN_SOCKET };

static int    g_conn = CONN_CONSOLE;
static SOCKET g_sock = INVALID_SOCKET;
static bool   g_wsa;
static HANDLE g_in, g_out;
static DWORD  g_in_mode, g_out_mode;
static bool   g_modes_saved;

/* The game the door runs itself (ANSI and JPEG XL modes) prints its console messages to stdout and stderr: they
 * must never reach the caller (in local mode the console is the caller), so the C library's streams go to NUL */
static void quiet_stdio(void)
{
    fflush(stdout);
    fflush(stderr);
    freopen("NUL", "w", stdout);
    freopen("NUL", "w", stderr);
}

bool plat_conn_open(int comm_type, long long handle)
{
    /* A crash must never leave a "this program has stopped working" box waiting on the BBS machine */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    if (comm_type == 2 && handle > 0)
    {
        WSADATA wsa;
        int type = 0, type_len = sizeof(type), on = 1;

        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return false;
        g_wsa = true;
        g_sock = (SOCKET)(uintptr_t)handle;
        if (getsockopt(g_sock, SOL_SOCKET, SO_TYPE, (char *)&type, &type_len) != 0 || type != SOCK_STREAM)
            return false;                   /* not a socket we were given: the drop file is wrong */
        /* Small writes (a key's echo, a frame's last bytes) go at once rather than waiting to be joined */
        setsockopt(g_sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
        g_conn = CONN_SOCKET;
        telnet_reset();
        quiet_stdio();
        return true;
    }

    /* Local: the console (or pipes, under a test harness). The door keeps a copy of the output handle of its own:
     * quiet_stdio closes the C library's, and the handle with it */
    g_in = GetStdHandle(STD_INPUT_HANDLE);
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    {
        HANDLE copy;
        if (DuplicateHandle(GetCurrentProcess(), g_out, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
        {
            g_out = copy;
            quiet_stdio();
        }
    }
    if (GetFileType(g_in) == FILE_TYPE_PIPE)
        g_conn = CONN_PIPE;
    else
    {
        g_conn = CONN_CONSOLE;
        if (GetConsoleMode(g_in, &g_in_mode) && GetConsoleMode(g_out, &g_out_mode))
        {
            g_modes_saved = true;
            SetConsoleMode(g_out, g_out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
        SetConsoleOutputCP(437);
    }
    return true;
}

void plat_conn_close(void)
{
    if (g_modes_saved)
    {
        SetConsoleMode(g_in, g_in_mode);
        SetConsoleMode(g_out, g_out_mode);
        g_modes_saved = false;
    }
    if (g_wsa)
    {
        /* The socket is the BBS's: never closed here, Mystic carries on with it after the door */
        WSACleanup();
        g_wsa = false;
    }
}

/* One wait on the socket: >0 bytes (telnet taken out, may be 0 after that), 0 nothing in time, -1 hung up */
static int socket_read(unsigned char *buf, int max, int timeout_ms)
{
    fd_set fds;
    struct timeval tv, *wait = NULL;
    int ready, n;

    if (timeout_ms >= 0)
    {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        wait = &tv;
    }
    FD_ZERO(&fds);
    FD_SET(g_sock, &fds);
    ready = select(0, &fds, NULL, NULL, wait);
    if (ready == SOCKET_ERROR)
        return -1;
    if (ready == 0)
        return 0;
    n = recv(g_sock, (char *)buf, max, 0);
    if (n == 0)
        return -1;                          /* the caller hung up */
    if (n == SOCKET_ERROR)
        return WSAGetLastError() == WSAEWOULDBLOCK ? 0 : -1;
    return (int)telnet_filter_in(buf, (size_t)n);
}

int plat_read(unsigned char *buf, int max, int timeout_ms)
{
    long deadline = plat_now_ms() + (timeout_ms > 0 ? timeout_ms : 0);

    for (;;)
    {
        long left = timeout_ms < 0 ? -1 : deadline - plat_now_ms();
        int n = 0;

        if (left < -1)
            left = 0;
        if (g_conn == CONN_SOCKET)
        {
            n = socket_read(buf, max, (int)left);
            if (n != 0)
                return n;
            /* nothing, or only telnet commands: wait on, if there's time left */
        }
        else if (g_conn == CONN_PIPE)
        {
            DWORD avail = 0, got = 0;
            if (!PeekNamedPipe(g_in, NULL, 0, NULL, &avail, NULL))
                return -1;
            if (avail > 0)
            {
                if (!ReadFile(g_in, buf, avail < (DWORD)max ? avail : (DWORD)max, &got, NULL) || got == 0)
                    return -1;
                return (int)got;
            }
            Sleep(5);
        }
        else
        {
            while (n < max && _kbhit())
                buf[n++] = (unsigned char)_getch();
            if (n > 0)
                return n;
            Sleep(5);
        }
        if (timeout_ms == 0 || (timeout_ms > 0 && plat_now_ms() >= deadline))
            return 0;
    }
}

static bool socket_send_all(const unsigned char *p, size_t len)
{
    while (len > 0)
    {
        int n = send(g_sock, (const char *)p, len > 65536 ? 65536 : (int)len, 0);
        if (n > 0)
        {
            p += n;
            len -= (size_t)n;
        }
        else if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
        {
            fd_set fds;
            struct timeval tv = { 0, 100000 };
            FD_ZERO(&fds);
            FD_SET(g_sock, &fds);
            select(0, NULL, &fds, NULL, &tv);
        }
        else
            return false;
    }
    return true;
}

bool plat_write(const void *data, size_t len)
{
    const unsigned char *p = data;

    if (g_conn == CONN_SOCKET)
    {
        unsigned char out[2 * 8192];
        while (len > 0)
        {
            size_t take = len > 8192 ? 8192 : len;
            size_t n = telnet_escape_out(p, take, out);
            if (!socket_send_all(out, n))
                return false;
            p += take;
            len -= take;
        }
        return true;
    }
    while (len > 0)
    {
        DWORD wrote = 0;
        if (!WriteFile(g_out, p, len > 65536 ? 65536 : (DWORD)len, &wrote, NULL) || wrote == 0)
            return false;
        p += wrote;
        len -= wrote;
    }
    return true;
}

long plat_out_queued(void)
{
    return -1;                              /* Winsock doesn't say; pacing goes by the terminal's answers instead */
}

/* From when the door started: a long is 32 bits on Windows, and a BBS machine's uptime can pass 24 days */
long plat_now_ms(void)
{
    static ULONGLONG start;
    if (start == 0)
        start = GetTickCount64() - 1;
    return (long)(GetTickCount64() - start);
}

void plat_sleep_ms(int ms)
{
    Sleep((DWORD)ms);
}

void plat_exe_dir(char *out, size_t size)
{
    DWORD len = GetModuleFileNameA(NULL, out, (DWORD)size);
    char *slash;

    if (len == 0 || len >= size)
    {
        snprintf(out, size, ".");
        return;
    }
    out[len] = '\0';
    slash = strrchr(out, '\\');
    if (slash == NULL)
        slash = strrchr(out, '/');
    if (slash != NULL)
        *slash = '\0';
}

void plat_mkdir(const char *path)
{
    CreateDirectoryA(path, NULL);
}

bool plat_replace(const char *from, const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

void *plat_lib_open(const char *name)
{
    /* Only from the door's own folder (never the current one or PATH), and the DLLs it needs from there too */
    char path[MAX_PATH + 64];
    size_t n;

    plat_exe_dir(path, MAX_PATH);
    n = strlen(path);
    snprintf(path + n, sizeof(path) - n, "\\%s", name);
    return (void *)LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
}

void *plat_lib_sym(void *lib, const char *name)
{
    FARPROC f = GetProcAddress((HMODULE)lib, name);
    void *p;
    memcpy(&p, &f, sizeof(p));
    return p;
}

/* ---- crash guard ---- */

static char   g_reset[256];
static size_t g_reset_len;

static LONG WINAPI on_crash(EXCEPTION_POINTERS *info)
{
    (void)info;
    if (g_reset_len > 0)
        plat_write(g_reset, g_reset_len);
    plat_conn_close();
    ExitProcess(3);
    return EXCEPTION_EXECUTE_HANDLER;
}

void plat_crash_guard(const char *reset, size_t len)
{
    if (reset == NULL)
    {
        SetUnhandledExceptionFilter(NULL);
        return;
    }
    g_reset_len = len < sizeof(g_reset) ? len : sizeof(g_reset);
    memcpy(g_reset, reset, g_reset_len);
    SetUnhandledExceptionFilter(on_crash);
}
