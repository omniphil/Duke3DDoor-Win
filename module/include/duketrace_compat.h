/*
 * duketrace_compat.h -- forced into every file with -include, so JFDuke3D's sources build unchanged.
 *
 *   exit()             The game leaves with exit() (gameexit, fatal errors). In the sandbox that has to send the
 *                      player's files to the BBS and close TERMinator's picture, so it goes to duketrace_exit
 *                      (src/duketrace.c).
 *   the file calls     There is no file system. DUKE3D.GRP and the sound font come from the pack the door sent, and
 *                      the player's duke3d.cfg and game0-9.sav live on the BBS, so open/read/lseek/close/fstat/stat/
 *                      access and fopen/fclose go through src/file_trace.c. The calls are function-like macros, so
 *                      "struct stat" and the like are left alone.
 *   mkdir, chdir       Folders only have to seem to work.
 */

#ifndef DUKETRACE_COMPAT_H
#define DUKETRACE_COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef _WIN32
/* The door's Windows build (../door): these declare the same calls, so they must come before the macros below */
#include <direct.h>
#include <io.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

void  duketrace_exit(int code) __attribute__((noreturn));
int   duketrace_open(const char *path, int flags, ...);
int   duketrace_close(int fd);
ssize_t duketrace_read(int fd, void *buf, size_t len);
ssize_t duketrace_write(int fd, const void *buf, size_t len);
off_t duketrace_lseek(int fd, off_t off, int whence);
int   duketrace_fstat(int fd, struct stat *buf);
int   duketrace_stat(const char *path, struct stat *buf);
int   duketrace_access(const char *path, int mode);
FILE *duketrace_fopen(const char *path, const char *mode);
int   duketrace_fclose(FILE *f);
int   duketrace_unlink(const char *path);

#ifdef __cplusplus
}
#endif

#define exit(code)          duketrace_exit(code)
#define open(...)           duketrace_open(__VA_ARGS__)
#define close(fd)           duketrace_close(fd)
#define read(fd, b, n)      duketrace_read(fd, b, n)
#define write(fd, b, n)     duketrace_write(fd, b, n)
#define lseek(fd, o, w)     duketrace_lseek(fd, o, w)
#define fstat(fd, b)        duketrace_fstat(fd, b)
#define stat(p, b)          duketrace_stat(p, b)
#define access(p, m)        duketrace_access(p, m)
#define fopen(p, m)         duketrace_fopen(p, m)
#define fclose(f)           duketrace_fclose(f)
#define unlink(p)           duketrace_unlink(p)
#define remove(p)           duketrace_unlink(p)
#define mkdir(...)          0        /* one argument on Windows, two elsewhere */
#define chdir(p)            0

#endif
