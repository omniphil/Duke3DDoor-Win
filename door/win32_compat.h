/*
 * win32_compat.h -- the two POSIX calls trace_door.c makes that Windows lacks, so that file stays an exact copy of
 * BBSGames/TraceDoor/trace_door.c (refreshed by its sync.sh) and still builds here. Force-included (-include) in the
 * Windows build only.
 *
 *   readlink("/proc/self/exe", ...)   the door's own path, from GetModuleFileName
 *   clock_gettime(CLOCK_MONOTONIC)    from GetTickCount64
 */

#ifndef WIN32_COMPAT_H
#define WIN32_COMPAT_H

#ifdef _WIN32

#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Declared here rather than including windows.h into every file */
__declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void *module, char *name, unsigned long size);
__declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);

static inline ssize_t door_readlink(const char *path, char *buf, size_t size)
{
    unsigned long len;

    if (strcmp(path, "/proc/self/exe") != 0 || size == 0)
        return -1;
    len = GetModuleFileNameA(NULL, buf, (unsigned long)size);
    if (len == 0 || len >= size)
        return -1;
    /* trace_door.c looks for the last '/' */
    for (unsigned long i = 0; i < len; i++)
        if (buf[i] == '\\')
            buf[i] = '/';
    return (ssize_t)len;
}
#define readlink door_readlink

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
static inline int door_clock_gettime(int clock, struct timespec *ts)
{
    unsigned long long ms = GetTickCount64();
    (void)clock;
    ts->tv_sec = (time_t)(ms / 1000);
    ts->tv_nsec = (long)(ms % 1000) * 1000000L;
    return 0;
}
#define clock_gettime door_clock_gettime

#endif /* _WIN32 */
#endif
