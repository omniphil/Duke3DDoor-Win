/*
 * door.c -- the BBS door's connection to the caller. The Wolfenstein 3D door's door.c, with everything that differs
 * between Linux and Windows moved into platform_*.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "door.h"
#include "platform.h"

DoorInfo door_info;

static time_t start_time;
static bool g_hung_up;

/* Bytes read from the caller but not yet taken */
static unsigned char g_in[4096];
static int g_in_pos, g_in_len;

static void read_drop_line(FILE *fp, char *buf, int maxlen)
{
    if (fgets(buf, maxlen, fp) == NULL) {
        buf[0] = '\0';
        return;
    }
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
        buf[--len] = '\0';
}

bool door_init(const char *drop_file_path)
{
    FILE *fp = NULL;
    char line[256];
    char path_buf[512];

    memset(&door_info, 0, sizeof(DoorInfo));
    door_info.local_mode = true;
    door_info.time_remaining = 60;
    strcpy(door_info.handle, "Player");
    strcpy(door_info.real_name, "Local User");
    strcpy(door_info.bbs_id, "LOCAL");

    start_time = time(NULL);

    if (drop_file_path != NULL && drop_file_path[0] != '\0') {
        size_t len = strlen(drop_file_path);
        if (len < sizeof(path_buf) - 15) {
            strcpy(path_buf, drop_file_path);
            if (path_buf[len-1] != '/' && path_buf[len-1] != '\\')
                strcat(path_buf, "/");
            strcat(path_buf, "door32.sys");
            fp = fopen(path_buf, "r");
            if (fp == NULL) {
                /* Mystic for Windows writes it in capitals; on Linux the name's case matters */
                strcpy(path_buf + strlen(path_buf) - 10, "DOOR32.SYS");
                fp = fopen(path_buf, "r");
            }
        }
        if (fp == NULL)
            fp = fopen(drop_file_path, "r");
    }
    if (fp == NULL) fp = fopen("door32.sys", "r");
    if (fp == NULL) fp = fopen("DOOR32.SYS", "r");

    if (fp != NULL) {
        door_info.local_mode = false;

        read_drop_line(fp, line, sizeof(line));
        door_info.comm_type = atoi(line);
        read_drop_line(fp, line, sizeof(line));
        door_info.comm_handle = strtoll(line, NULL, 10);
        read_drop_line(fp, line, sizeof(line));
        door_info.baud_rate = atoi(line);
        read_drop_line(fp, door_info.bbs_id, sizeof(door_info.bbs_id));
        read_drop_line(fp, line, sizeof(line));
        door_info.user_record = atoi(line);
        read_drop_line(fp, door_info.real_name, sizeof(door_info.real_name));
        read_drop_line(fp, door_info.handle, sizeof(door_info.handle));
        read_drop_line(fp, line, sizeof(line));
        door_info.security_level = atoi(line);
        read_drop_line(fp, line, sizeof(line));
        door_info.time_remaining = atoi(line);
        fclose(fp);

        if (door_info.handle[0] == '\0')
            strcpy(door_info.handle, door_info.real_name);
        if (door_info.real_name[0] == '\0')
            strcpy(door_info.real_name, door_info.handle);
    }

    return plat_conn_open(door_info.local_mode ? COMM_LOCAL : door_info.comm_type, door_info.comm_handle);
}

int door_time_remaining(void)
{
    int elapsed = (int)(time(NULL) - start_time) / 60;
    int remaining = door_info.time_remaining - elapsed;
    return remaining > 0 ? remaining : 0;
}

/* The caller is gone: nothing more can be said or kept, so the door ends here */
static void hung_up_exit(void)
{
    door_cleanup();
    exit(0);
}

void door_write_raw(const char *data, size_t len)
{
    if (g_hung_up || len == 0)
        return;
    if (!plat_write(data, len))
        g_hung_up = true;
}

void door_write(const char *str)
{
    door_write_raw(str, strlen(str));
}

void door_write_char(char c)
{
    door_write_raw(&c, 1);
}

int door_read_raw(unsigned char *buf, int max, int timeout_ms)
{
    if (g_in_pos < g_in_len) {
        int n = g_in_len - g_in_pos < max ? g_in_len - g_in_pos : max;
        memcpy(buf, g_in + g_in_pos, (size_t)n);
        g_in_pos += n;
        return n;
    }
    if (g_hung_up)
        return -1;
    int n = plat_read(buf, max, timeout_ms);
    if (n < 0)
        g_hung_up = true;
    return n;
}

bool door_hung_up(void)
{
    return g_hung_up;
}

/* Fills the input buffer, waiting up to timeout_ms. False if nothing came. */
static bool fill(int timeout_ms)
{
    if (g_in_pos < g_in_len)
        return true;
    g_in_pos = g_in_len = 0;
    int n = g_hung_up ? -1 : plat_read(g_in, sizeof(g_in), timeout_ms);
    if (n < 0) {
        g_hung_up = true;
        hung_up_exit();
    }
    g_in_len = n;
    return n > 0;
}

int door_read_char_timeout(int timeout_ms)
{
    return fill(timeout_ms) ? g_in[g_in_pos++] : -1;
}

int door_read_char(void)
{
    /* Waits for a key, but not past the caller's time */
    while (!fill(1000))
        if (door_time_remaining() <= 0)
            return -1;
    return g_in[g_in_pos++];
}

bool door_kbhit(void)
{
    return fill(0);
}

int door_read_line(char *buffer, int max_len)
{
    int pos = 0;
    buffer[0] = '\0';

    while (pos < max_len - 1) {
        int c = door_read_char();
        if (c < 0)
            break;
        if (c == '\r' || c == '\n') {
            door_write("\r\n");
            break;
        } else if (c == 8 || c == 127) {
            if (pos > 0) {
                pos--;
                door_write("\b \b");
            }
        } else if (c == 27) {
            buffer[0] = '\0';
            return 0;
        } else if (c >= 32 && c < 127) {
            buffer[pos++] = (char)c;
            door_write_char((char)c);
        }
    }
    buffer[pos] = '\0';
    return pos;
}

void door_cleanup(void)
{
    plat_conn_close();
}
