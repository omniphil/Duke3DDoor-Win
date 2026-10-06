/*
 * file_trace.c -- JFDuke3D's files, without a file system. Every open/read/lseek/close/fstat/stat/access and
 * fopen/fclose the game makes comes here (include/duketrace_compat.h). Folders in a path are ignored and names are
 * matched in lower case.
 *
 * There are two kinds of file:
 *   - The game data (duke3d.grp, timgm6mb.sf2), read-only, from the pack the door sent as an asset (duketrace.c).
 *   - The player's own files: duke3d.cfg (settings, keys) and game0-9.sav. These live on the BBS, per player. The door
 *     sends them at the start; whenever the game writes one, the new contents go back up a piece at a time
 *     (duketrace_user_files_pump), so they're safe even if the call drops.
 *
 * Descriptors from open() are numbers of our own (FD_BASE and up) over the bytes in memory. fopen() reading is
 * fmemopen over the bytes, writing is open_memstream: real stdio streams, so the game's own fread/fwrite/fprintf
 * calls work on them unchanged. Anything else (logs, screenshots, demos, the GRP scan cache) has nowhere to go.
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "trace_api.h"
#include "duketrace.h"

#undef open
#undef close
#undef read
#undef write
#undef lseek
#undef fstat
#undef stat
#undef access
#undef fopen
#undef fclose
#undef unlink
#undef remove

/* ---- the player's files ---- */

#define CHUNK     3000                  /* bytes per message to the door, header and all under TRACE_SEND_MAX */
#define USER_MAX  (4 * 1024 * 1024)     /* the largest player file taken from the door */

typedef struct
{
    const char *name;
    uint8_t    *data;       /* what the game sees */
    size_t      size;
    int         present;    /* the player has this file (the door sent it, or the game wrote it) */

    /* receiving from the door */
    uint8_t    *incoming;
    size_t      incoming_total, incoming_have;

    /* sending to the door: a snapshot, so the game can write again while an older copy is going up */
    uint8_t    *outgoing;
    size_t      outgoing_size, outgoing_sent;
    int         outgoing_busy;
} user_file_t;

/* Only these are kept, and only these may go to the BBS: the door should accept nothing else either */
static user_file_t g_user_files[] =
{
    { "duke3d.cfg" },
    { "game0.sav" }, { "game1.sav" }, { "game2.sav" }, { "game3.sav" }, { "game4.sav" },
    { "game5.sav" }, { "game6.sav" }, { "game7.sav" }, { "game8.sav" }, { "game9.sav" },
};
#define USER_FILE_COUNT (sizeof(g_user_files) / sizeof(g_user_files[0]))

static user_file_t *find_user_file(const char *name)
{
    for (size_t i = 0; i < USER_FILE_COUNT; i++)
        if (strcmp(g_user_files[i].name, name) == 0)
            return &g_user_files[i];
    return NULL;
}

/* A piece of one of the player's files from the door. Pieces come in order; the file is only used once it's whole. */
void duketrace_user_file_received(const char *name, size_t off, size_t total, const uint8_t *data, size_t len)
{
    user_file_t *uf = find_user_file(name);

    if (uf == NULL || total > USER_MAX)
        return;
    if (off == 0)
    {
        free(uf->incoming);
        uf->incoming = (uint8_t *)malloc(total > 0 ? total : 1);
        uf->incoming_total = total;
        uf->incoming_have = 0;
    }
    if (uf->incoming == NULL || off != uf->incoming_have || off + len > uf->incoming_total)
    {
        duketrace_log("duke3d: a piece of %s arrived out of order; ignoring it", name);
        free(uf->incoming);
        uf->incoming = NULL;
        return;
    }
    memcpy(uf->incoming + off, data, len);
    uf->incoming_have += len;
    if (uf->incoming_have == uf->incoming_total)
    {
        free(uf->data);
        uf->data = uf->incoming;
        uf->size = uf->incoming_total;
        uf->present = 1;
        uf->incoming = NULL;
        duketrace_log("duke3d: %s from the BBS, %zu bytes", name, uf->size);
    }
}

static const uint8_t *user_file(const char *name, size_t *size)
{
    user_file_t *uf = find_user_file(name);
    if (uf == NULL || !uf->present)
        return NULL;
    *size = uf->size;
    return uf->data;
}

/* The game wrote a file: keep it, and send it to the BBS unless it's what the BBS already has */
static void user_file_written(user_file_t *uf, const uint8_t *data, size_t size)
{
    uint8_t *copy;

    if (uf->present && uf->size == size && (size == 0 || memcmp(uf->data, data, size) == 0))
        return;

    copy = (uint8_t *)malloc(size > 0 ? size : 1);
    if (copy == NULL)
        return;
    memcpy(copy, data, size);
    free(uf->data);
    uf->data = copy;
    uf->size = size;
    uf->present = 1;

    /* The newest copy replaces one still on its way: the door starts again when a piece at offset 0 arrives */
    free(uf->outgoing);
    uf->outgoing = (uint8_t *)malloc(size > 0 ? size : 1);
    if (uf->outgoing == NULL)
    {
        uf->outgoing_busy = 0;
        return;
    }
    memcpy(uf->outgoing, data, size);
    uf->outgoing_size = size;
    uf->outgoing_sent = 0;
    uf->outgoing_busy = 1;
}

/*
 * Sends what the link will take right now:
 *   put name=<n> off=<o> total=<t>\n<bytes>
 * Returns 1 while anything is still waiting to go.
 */
int duketrace_user_files_pump(void)
{
    static uint8_t message[CHUNK + 128];
    int waiting = 0;

    for (size_t i = 0; i < USER_FILE_COUNT; i++)
    {
        user_file_t *uf = &g_user_files[i];

        while (uf->outgoing_busy)
        {
            size_t len = uf->outgoing_size - uf->outgoing_sent;
            int head;

            if (len > CHUNK)
                len = CHUNK;
            head = snprintf((char *)message, 128, "put name=%s off=%zu total=%zu\n",
                            uf->name, uf->outgoing_sent, uf->outgoing_size);
            memcpy(message + head, uf->outgoing + uf->outgoing_sent, len);
            if (trace_send_room() < head + (int)len || trace_send(message, head + (int32_t)len) <= 0)
                break;      /* the link is busy: the rest goes on a later call */

            uf->outgoing_sent += len;
            if (uf->outgoing_sent >= uf->outgoing_size)
            {
                duketrace_log("duke3d: %s saved to the BBS, %zu bytes", uf->name, uf->outgoing_size);
                free(uf->outgoing);
                uf->outgoing = NULL;
                uf->outgoing_busy = 0;
            }
        }
        waiting |= uf->outgoing_busy;
    }
    return waiting;
}

/* ---- names ---- */

/* The file's own name, lower case: "./GAME0.SAV" -> "game0.sav" */
static void leaf(const char *path, char *out, size_t size)
{
    const char *slash = strrchr(path, '/'), *back = strrchr(path, '\\');
    size_t i = 0;

    if (back != NULL && (slash == NULL || back > slash))
        slash = back;
    if (slash != NULL)
        path = slash + 1;
    for (; path[i] && i + 1 < size; i++)
        out[i] = (char)tolower((unsigned char)path[i]);
    out[i] = 0;
}

static const uint8_t *find_file(const char *path, size_t *size)
{
    char name[64];
    const uint8_t *data;

    leaf(path, name, sizeof(name));
    data = duketrace_data_file(name, size);
    if (data == NULL)
        data = user_file(name, size);
    return data;
}

/* ---- descriptors ---- */

#define FD_BASE 0x4000
#define FD_MAX  32

typedef struct
{
    int            used;
    const uint8_t *data;        /* reading */
    size_t         size, pos;
    user_file_t   *writing;     /* writing one of the player's files */
    uint8_t       *wbuf;
    size_t         wsize, wcap;
} vfd_t;

static vfd_t g_fds[FD_MAX];

static vfd_t *vfd(int fd)
{
    if (fd < FD_BASE || fd >= FD_BASE + FD_MAX || !g_fds[fd - FD_BASE].used)
        return NULL;
    return &g_fds[fd - FD_BASE];
}

static int new_fd(void)
{
    for (int i = 0; i < FD_MAX; i++)
        if (!g_fds[i].used)
        {
            memset(&g_fds[i], 0, sizeof(g_fds[i]));
            g_fds[i].used = 1;
            return FD_BASE + i;
        }
    errno = EMFILE;
    return -1;
}

int duketrace_open(const char *path, int flags, ...)
{
    int fd;
    vfd_t *f;

    if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC)))
    {
        char name[64];
        user_file_t *uf;
        leaf(path, name, sizeof(name));
        uf = find_user_file(name);
        if (uf == NULL)
        {
            errno = EACCES;     /* a screenshot or a log: there's nowhere to keep it */
            return -1;
        }
        if ((flags & O_EXCL) && uf->present)
        {
            errno = EEXIST;
            return -1;
        }
        fd = new_fd();
        if (fd < 0)
            return -1;
        f = vfd(fd);
        f->writing = uf;
        if (!(flags & O_TRUNC) && uf->present && uf->size > 0)
        {
            f->wbuf = (uint8_t *)malloc(uf->size);
            if (f->wbuf != NULL)
            {
                memcpy(f->wbuf, uf->data, uf->size);
                f->wsize = f->wcap = uf->size;
            }
        }
        return fd;
    }

    {
        size_t size = 0;
        const uint8_t *data = find_file(path, &size);
        if (data == NULL)
        {
            errno = ENOENT;
            return -1;
        }
        fd = new_fd();
        if (fd < 0)
            return -1;
        f = vfd(fd);
        f->data = data;
        f->size = size;
        return fd;
    }
}

int duketrace_close(int fd)
{
    vfd_t *f = vfd(fd);
    if (f == NULL)
    {
        errno = EBADF;
        return -1;
    }
    if (f->writing != NULL)
        user_file_written(f->writing, f->wbuf, f->wsize);
    free(f->wbuf);
    memset(f, 0, sizeof(*f));
    return 0;
}

ssize_t duketrace_read(int fd, void *buf, size_t len)
{
    vfd_t *f = vfd(fd);
    if (f == NULL || f->writing != NULL)
    {
        errno = EBADF;
        return -1;
    }
    if (f->pos >= f->size)
        return 0;
    if (len > f->size - f->pos)
        len = f->size - f->pos;
    memcpy(buf, f->data + f->pos, len);
    f->pos += len;
    return (ssize_t)len;
}

ssize_t duketrace_write(int fd, const void *buf, size_t len)
{
    vfd_t *f = vfd(fd);
    if (f == NULL || f->writing == NULL)
    {
        if (fd == 1 || fd == 2)
            return (ssize_t)len;    /* stdout and stderr go nowhere */
        errno = EBADF;
        return -1;
    }
    if (f->pos + len > USER_MAX)
    {
        errno = ENOSPC;
        return -1;
    }
    if (f->pos + len > f->wcap)
    {
        size_t cap = f->wcap ? f->wcap : 4096;
        uint8_t *grown;
        while (cap < f->pos + len)
            cap *= 2;
        grown = (uint8_t *)realloc(f->wbuf, cap);
        if (grown == NULL)
        {
            errno = ENOMEM;
            return -1;
        }
        f->wbuf = grown;
        f->wcap = cap;
    }
    if (f->pos > f->wsize)
        memset(f->wbuf + f->wsize, 0, f->pos - f->wsize);
    memcpy(f->wbuf + f->pos, buf, len);
    f->pos += len;
    if (f->pos > f->wsize)
        f->wsize = f->pos;
    return (ssize_t)len;
}

off_t duketrace_lseek(int fd, off_t off, int whence)
{
    vfd_t *f = vfd(fd);
    off_t base, end;
    if (f == NULL)
    {
        errno = EBADF;
        return -1;
    }
    end = (off_t)(f->writing != NULL ? f->wsize : f->size);
    base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (off_t)f->pos : end;
    if (base + off < 0)
    {
        errno = EINVAL;
        return -1;
    }
    f->pos = (size_t)(base + off);
    return (off_t)f->pos;
}

int duketrace_fstat(int fd, struct stat *buf)
{
    vfd_t *f = vfd(fd);
    if (f == NULL)
    {
        errno = EBADF;
        return -1;
    }
    memset(buf, 0, sizeof(*buf));
    buf->st_mode = S_IFREG | 0644;
    buf->st_size = (off_t)(f->writing != NULL ? f->wsize : f->size);
    return 0;
}

/* Folders (search paths) don't exist, so the game looks for everything in "./": data and player files only */
int duketrace_stat(const char *path, struct stat *buf)
{
    size_t size = 0;

    memset(buf, 0, sizeof(*buf));
    if (find_file(path, &size) != NULL)
    {
        buf->st_mode = S_IFREG | 0644;
        buf->st_size = (off_t)size;
        return 0;
    }
    errno = ENOENT;
    return -1;
}

int duketrace_access(const char *path, int mode)
{
    size_t size;
    (void)mode;
    if (find_file(path, &size) != NULL)
        return 0;
    errno = ENOENT;
    return -1;
}

/* ---- stdio ---- */

#ifdef _WIN32
/*
 * Windows has neither fmemopen nor open_memstream (only the door's own copy of the game, ../door, is ever built for
 * Windows). A temporary file stands in for both: in %TMP%, deleted when closed ("D"), and read back whole before that
 * when the game wrote it.
 */
static FILE *temp_stream(void)
{
    char *name = _tempnam(NULL, "duke");
    FILE *f = name != NULL ? fopen(name, "w+bD") : NULL;
    free(name);
    return f;
}

static FILE *fmemopen(void *data, size_t size, const char *mode)
{
    FILE *f = temp_stream();
    (void)mode;
    if (f != NULL && (fwrite(data, 1, size, f) != size || fseek(f, 0, SEEK_SET) != 0))
    {
        fclose(f);
        f = NULL;
    }
    return f;
}

static FILE *open_memstream(char **buffer, size_t *size)
{
    *buffer = NULL;
    *size = 0;
    return temp_stream();
}

/* What the game wrote, before the file goes */
static void read_back(FILE *f, char **buffer, size_t *size)
{
    long len;

    if (fflush(f) != 0 || fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0)
        return;
    *buffer = (char *)malloc(len > 0 ? (size_t)len : 1);
    if (*buffer != NULL)
        *size = fread(*buffer, 1, (size_t)len, f);
}
#endif

typedef struct
{
    FILE        *f;
    char        *buffer;
    size_t       size;
    user_file_t *user;
} writer_t;

static writer_t g_writers[4];

FILE *duketrace_fopen(const char *path, const char *mode)
{
    char name[64];
    const uint8_t *data;
    size_t size = 0;

    leaf(path, name, sizeof(name));
    if (strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL || strchr(mode, '+') != NULL)
    {
        user_file_t *uf = find_user_file(name);
        writer_t *w = NULL;

        if (uf == NULL)
        {
            errno = EACCES;     /* the log, the GRP scan cache, a demo: there's nowhere to keep them */
            return NULL;
        }
        for (size_t i = 0; i < sizeof(g_writers) / sizeof(g_writers[0]); i++)
            if (g_writers[i].f == NULL)
            {
                w = &g_writers[i];
                break;
            }
        if (w == NULL)
        {
            errno = EMFILE;
            return NULL;
        }
        w->buffer = NULL;
        w->size = 0;
        w->f = open_memstream(&w->buffer, &w->size);
        w->user = uf;
        return w->f;
    }

    data = find_file(path, &size);
    if (data == NULL || size == 0)      /* fmemopen won't take an empty buffer, and an empty file is no use */
    {
        errno = ENOENT;
        return NULL;
    }
    return fmemopen((void *)data, size, "rb");
}

/* Closing a file the game wrote is what sends it to the BBS: only whole, successfully written files go */
int duketrace_fclose(FILE *f)
{
    writer_t *w = NULL;
    int result;

    if (f == NULL)
        return EOF;
    for (size_t i = 0; i < sizeof(g_writers) / sizeof(g_writers[0]); i++)
        if (g_writers[i].f == f)
            w = &g_writers[i];
#ifdef _WIN32
    if (w != NULL)
        read_back(f, &w->buffer, &w->size);
#endif
    result = fclose(f);
    if (w != NULL)
    {
        if (result == 0)
            user_file_written(w->user, (const uint8_t *)w->buffer, w->size);
        free(w->buffer);
        w->buffer = NULL;
        w->f = NULL;
    }
    return result;
}

/* A save being overwritten may be deleted first; the new one follows at once, so only this copy forgets it */
int duketrace_unlink(const char *path)
{
    char name[64];
    user_file_t *uf;

    leaf(path, name, sizeof(name));
    uf = find_user_file(name);
    if (uf == NULL || !uf->present)
    {
        errno = ENOENT;
        return -1;
    }
    uf->present = 0;
    return 0;
}
