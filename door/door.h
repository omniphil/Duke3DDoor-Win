/*
 * door.h -- the BBS door's connection to the caller: the drop file, reading and writing, the caller's time.
 * The Wolfenstein 3D door's door.h, on top of platform.h so the same door runs on Linux and Windows.
 */

#ifndef DOOR_H
#define DOOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_USERNAME 64
#define MAX_BBSID    32

/* Communication types from door32.sys */
#define COMM_LOCAL   0
#define COMM_SERIAL  1
#define COMM_TELNET  2

typedef struct {
    int comm_type;                  /* 0 = local, 1 = serial, 2 = telnet */
    long long comm_handle;          /* the socket (Windows: the inherited Winsock SOCKET) */
    int baud_rate;
    char bbs_id[MAX_BBSID];
    int user_record;
    char real_name[MAX_USERNAME];
    char handle[MAX_USERNAME];
    int security_level;
    int time_remaining;             /* minutes */
    bool local_mode;                /* no drop file */
} DoorInfo;

extern DoorInfo door_info;

/* Reads door32.sys (from the folder or file given, else the current folder) and sets the connection up */
bool door_init(const char *drop_file_path);

/* Minutes the caller has left */
int door_time_remaining(void);

/* Output */
void door_write(const char *str);
void door_write_char(char c);
void door_write_raw(const char *data, size_t len);

/* Input. Once the caller has hung up these end the door (there is no one left to talk to): the menus and TRACE
 * need nothing more. A play loop that must notice a hang-up itself uses door_read_raw. */
int door_read_char(void);
int door_read_char_timeout(int timeout_ms);       /* -1 if nothing came in time */
bool door_kbhit(void);
int door_read_line(char *buffer, int max_len);

/* Whatever the caller has sent, up to max bytes, waiting up to timeout_ms: > 0 bytes, 0 nothing, -1 hung up.
 * Takes what door_read_char's buffer still holds first. Never ends the door. */
int door_read_raw(unsigned char *buf, int max, int timeout_ms);

/* Whether the caller has hung up */
bool door_hung_up(void);

void door_cleanup(void);

#endif
