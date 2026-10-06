/*
 * platform.h -- everything the door does differently on Linux and on Windows, in one place.
 *
 *   platform_posix.c   Linux (and any POSIX box): the caller is on stdin/stdout, a pseudo-terminal the BBS made
 *                      (Mystic for Linux does the telnet itself), put into raw mode.
 *   platform_win32.c   Windows (Mystic for Windows): DOOR32.SYS line 1 is 2 (telnet) and line 2 an inherited Winsock
 *                      SOCKET, the caller's telnet connection itself. The door talks on it with send/recv, and does the
 *                      telnet layer (telnet.c): 0xFF doubled going out, IAC commands taken out coming in. With no drop
 *                      file (or comm type 0) it uses the console, for testing.
 *
 * The rest of the door (door.c and up) only ever calls these.
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

/* Sets the connection up. comm_type and handle are DOOR32.SYS lines 1 and 2 (0, 0 with no drop file). */
bool plat_conn_open(int comm_type, long long handle);

/* Puts the connection back as it was (the terminal's mode on Linux; Winsock on Windows). Safe to call twice. */
void plat_conn_close(void);

/*
 * Reads what the caller has sent, waiting up to timeout_ms (0: don't wait, -1: forever).
 * Returns the number of bytes (> 0), 0 when nothing came in time, -1 when the caller has hung up.
 */
int plat_read(unsigned char *buf, int max, int timeout_ms);

/* Writes all of it, waiting as the link needs. Returns false when the caller has hung up. */
bool plat_write(const void *data, size_t len);

/* Bytes written but not yet sent on to the caller, or -1 when the platform can't tell (for link pacing) */
long plat_out_queued(void);

/* Milliseconds from a fixed point (monotonic), and sleeping */
long plat_now_ms(void);
void plat_sleep_ms(int ms);

/* The folder the door's own binary is in (no trailing separator); "." if it can't be found */
void plat_exe_dir(char *out, size_t size);

/* Makes one folder (its parent must exist); fine if it's already there */
void plat_mkdir(const char *path);

/* Renames from over to, replacing to if it exists (rename() won't on Windows). False on failure. */
bool plat_replace(const char *from, const char *to);

/* A shared library: libjxl.so.N on Linux; on Windows, a DLL taken only from the door's own folder */
void *plat_lib_open(const char *name);
void *plat_lib_sym(void *lib, const char *name);

/*
 * If the door crashes, these bytes go to the caller first (put the terminal back: leave graphics modes, show the
 * cursor), then it ends. On Linux: SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT; on Windows: an unhandled exception. The
 * bytes are copied; NULL turns it off.
 */
void plat_crash_guard(const char *reset, size_t len);

#endif
