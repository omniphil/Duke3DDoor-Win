/*
 * files.c -- the player's Duke Nukem 3D files, kept on the BBS, one set per player. (The Wolfenstein 3D door's files.c
 * with Duke's names, and the folder and renaming through platform.h so it works on Windows too.)
 *
 * The game runs in a sandbox on the player's PC, or in this door's own process in ANSI mode, and has nowhere of its
 * own to keep anything. So its files live here instead, and follow the player whichever machine they call from:
 *
 *   duke3d.cfg               settings and key bindings (JFDuke3D's SETUPFILENAME)
 *   game0.sav - game9.sav    the ten saved-game slots (JFDuke3D's menues.c)
 *
 * The ANSI mode keeps its own config (ansi-duke3d.cfg on disk, still duke3d.cfg to the game): it starts the game with
 * settings of its own, which the game then saves, and those mustn't follow the player into TRACE.
 *
 * They're all small, so the door sends every one at the start and the game sends each back whenever it writes it.
 * Only these names are accepted, so nothing the game says can reach any other file.
 *
 * Files live in saves/<player>/ beside the door binary. A file arriving is written beside the old one and renamed
 * over it only once it's whole, and the one it replaces is kept as <name>.bak, so a dropped call can't lose a save.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "platform.h"

#define MAX_FILE_BYTES (1024 * 1024)

static const char *const g_names[] = {
    "duke3d.cfg",
    "game0.sav", "game1.sav", "game2.sav", "game3.sav", "game4.sav",
    "game5.sav", "game6.sav", "game7.sav", "game8.sav", "game9.sav",
};
#define NAME_COUNT (sizeof(g_names) / sizeof(g_names[0]))

#define DIR_MAX 512
static char g_dir[DIR_MAX];
static char g_player[80];
static bool g_ansi_config;

/* The file on disk for a name the game uses: duke3d.cfg is ansi-duke3d.cfg in ANSI mode */
static const char *disk_name(const char *name)
{
    return g_ansi_config && strcmp(name, "duke3d.cfg") == 0 ? "ansi-duke3d.cfg" : name;
}

void files_use_ansi_config(bool on)
{
    g_ansi_config = on;
}

/* A file on its way up from the game, one per name */
static struct
{
    unsigned char *data;
    size_t         have, total;
} g_incoming[NAME_COUNT];

static int name_index(const char *name)
{
    for (size_t i = 0; i < NAME_COUNT; i++)
        if (strcmp(g_names[i], name) == 0)
            return (int)i;
    return -1;
}

/*
 * A folder name for this player: their handle, plus the BBS's own user number.
 *
 * The handle alone isn't enough to tell two people apart, because cutting it down to plain characters can make two
 * different handles the same ("User" and "U.s.e.r" both become "user"). The user number is unique on the board, so
 * the two together can't collide. Cutting the handle down also means it can only ever name a folder inside saves/.
 */
void files_init(const char *player, int user_number)
{
    char base[DIR_MAX - 128];
    char name[80];
    char handle[48];
    size_t n = 0;

    for (const char *p = player; *p != '\0' && n < sizeof(handle) - 1; p++)
        if (isalnum((unsigned char)*p) || *p == '-' || *p == '_')
            handle[n++] = (char)tolower((unsigned char)*p);
    handle[n] = '\0';
    if (n == 0)
        snprintf(handle, sizeof(handle), "player");

    if (user_number > 0)
        snprintf(name, sizeof(name), "%s-%d", handle, user_number);
    else
        snprintf(name, sizeof(name), "%s", handle);

    snprintf(g_player, sizeof(g_player), "%s", name);

    plat_exe_dir(base, sizeof(base));
    snprintf(g_dir, sizeof(g_dir), "%s/saves", base);
    plat_mkdir(g_dir);
    snprintf(g_dir, sizeof(g_dir), "%s/saves/%s", base, name);
    plat_mkdir(g_dir);
}

const char *files_folder(void)
{
    return g_dir;
}

const char *files_player(void)
{
    return g_player;
}

/* Each file this player has, whole, in pieces of FILES_CHUNK:  file name=<n> off=<o> total=<t>\n<bytes> */
void files_send_all(void (*send)(const char *head, const void *payload, size_t len))
{
    for (size_t i = 0; i < NAME_COUNT; i++)
    {
        char path[DIR_MAX + 32], head[96];
        unsigned char *data;
        long size;
        FILE *fp;

        snprintf(path, sizeof(path), "%s/%s", g_dir, disk_name(g_names[i]));
        fp = fopen(path, "rb");
        if (fp == NULL)
            continue;
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (size <= 0 || size > MAX_FILE_BYTES || (data = malloc((size_t)size)) == NULL)
        {
            fclose(fp);
            continue;
        }
        if (fread(data, 1, (size_t)size, fp) == (size_t)size)
        {
            for (long off = 0; off < size; off += FILES_CHUNK)
            {
                size_t len = size - off < FILES_CHUNK ? (size_t)(size - off) : FILES_CHUNK;
                snprintf(head, sizeof(head), "file name=%s off=%ld total=%ld", g_names[i], off, size);
                send(head, data + off, len);
            }
        }
        free(data);
        fclose(fp);
    }
}

/* Pieces arrive in order; a piece at offset 0 starts the file again (the game wrote a newer copy mid-send). */
int files_receive_chunk(const char *name, size_t offset, size_t total, const unsigned char *data, size_t size)
{
    int i = name_index(name);
    char path[DIR_MAX + 32], temp[DIR_MAX + 40], backup[DIR_MAX + 40];
    FILE *fp;

    if (i < 0 || total == 0 || total > MAX_FILE_BYTES)
        return 0;

    if (offset == 0)
    {
        free(g_incoming[i].data);
        g_incoming[i].data = malloc(total);
        g_incoming[i].have = 0;
        g_incoming[i].total = total;
    }
    if (g_incoming[i].data == NULL || offset != g_incoming[i].have || total != g_incoming[i].total ||
        offset + size > total)
    {
        /* A piece went missing: drop the whole file rather than keep one with a hole in it */
        free(g_incoming[i].data);
        g_incoming[i].data = NULL;
        return 0;
    }
    memcpy(g_incoming[i].data + offset, data, size);
    g_incoming[i].have += size;
    if (g_incoming[i].have < total)
        return 0;

    snprintf(path, sizeof(path), "%s/%s", g_dir, disk_name(name));
    snprintf(temp, sizeof(temp), "%s/%s.new", g_dir, disk_name(name));
    snprintf(backup, sizeof(backup), "%s/%s.bak", g_dir, disk_name(name));
    fp = fopen(temp, "wb");
    if (fp != NULL)
    {
        int ok = fwrite(g_incoming[i].data, 1, total, fp) == total;
        ok &= fclose(fp) == 0;
        if (ok)
        {
            plat_replace(path, backup);   /* the previous copy, in case this one turns out to be bad */
            ok = plat_replace(temp, path);
        }
        if (!ok)
            remove(temp);
    }
    free(g_incoming[i].data);
    g_incoming[i].data = NULL;
    return 1;
}
