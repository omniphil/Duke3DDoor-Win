/*
 * telnet.c -- the telnet layer (RFC 854) for a door that is handed the telnet socket itself.
 *
 * The BBS has already negotiated the connection (binary, echo, suppress go-ahead) before it starts the door, so the
 * door doesn't negotiate: whatever the caller's telnet client still sends -- an option it answers, a window size --
 * is taken out and ignored. What's left is the caller's own bytes:
 *   IAC IAC            one 0xFF
 *   IAC WILL/WONT/DO/DONT <option>   dropped
 *   IAC SB ... IAC SE  dropped (a subnegotiation: window size, terminal type)
 *   IAC <other>        dropped (NOP, go-ahead, are-you-there...)
 *   CR NUL             CR (a telnet client's Enter outside binary mode)
 * Going out, 0xFF is doubled. TRACE's binary frames never carry a raw 0xFF (trace_door.c escapes it), but CP437
 * pictures and text can (0xFF is a non-breaking space).
 */

#include "telnet.h"

#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240

enum { S_DATA, S_IAC, S_OPTION, S_SB, S_SB_IAC, S_CR };

static int g_state = S_DATA;

void telnet_reset(void)
{
    g_state = S_DATA;
}

size_t telnet_filter_in(unsigned char *buf, size_t len)
{
    size_t out = 0;

    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = buf[i];

        switch (g_state)
        {
        case S_CR:
            g_state = S_DATA;
            if (c == 0)
                break;                      /* CR NUL: the NUL goes; anything else is an ordinary byte */
            /* fall through */
        case S_DATA:
            if (c == IAC)
                g_state = S_IAC;
            else
            {
                buf[out++] = c;
                if (c == '\r')
                    g_state = S_CR;
            }
            break;
        case S_IAC:
            if (c == IAC)
            {
                buf[out++] = IAC;
                g_state = S_DATA;
            }
            else if (c >= WILL && c <= DONT)
                g_state = S_OPTION;
            else if (c == SB)
                g_state = S_SB;
            else
                g_state = S_DATA;
            break;
        case S_OPTION:
            g_state = S_DATA;
            break;
        case S_SB:
            if (c == IAC)
                g_state = S_SB_IAC;
            break;
        case S_SB_IAC:
            g_state = c == SE ? S_DATA : S_SB;
            break;
        }
    }
    return out;
}

size_t telnet_escape_out(const unsigned char *data, size_t len, unsigned char *out)
{
    size_t o = 0;

    for (size_t i = 0; i < len; i++)
    {
        out[o++] = data[i];
        if (data[i] == IAC)
            out[o++] = IAC;
    }
    return o;
}
