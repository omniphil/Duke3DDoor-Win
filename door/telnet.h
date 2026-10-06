/*
 * telnet.h -- the telnet layer, for the Windows door, which is handed the caller's telnet socket itself (Mystic for
 * Windows; on Linux Mystic does this and gives the door plain bytes on a pseudo-terminal). See telnet.c.
 */

#ifndef TELNET_H
#define TELNET_H

#include <stddef.h>

/* Takes the telnet commands out of what the caller sent (in place). Keeps its state between calls, so a command split
 * across two reads is still taken out whole. Returns how many bytes of the caller's own are left. */
size_t telnet_filter_in(unsigned char *buf, size_t len);

/* Copies data to out, doubling every 0xFF (IAC) so it arrives as itself. out needs room for 2 * len. Returns its
 * length. */
size_t telnet_escape_out(const unsigned char *data, size_t len, unsigned char *out);

/* Starts the input side afresh */
void telnet_reset(void);

#endif
