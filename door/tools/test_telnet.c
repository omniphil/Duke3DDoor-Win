/* test_telnet.c -- checks telnet.c (the Windows door's telnet layer) on any machine: make test-telnet */

#include <stdio.h>
#include <string.h>

#include "telnet.h"

static int failures;

static void check(const char *what, const unsigned char *in, size_t in_len, const unsigned char *want, size_t want_len,
                  size_t split)
{
    unsigned char buf[256], got[256];
    size_t n = 0;

    telnet_reset();
    memcpy(buf, in, in_len);
    /* in two reads, split at `split`, so a command broken across reads is tested too */
    n = telnet_filter_in(buf, split);
    memcpy(got, buf, n);
    memcpy(buf, in + split, in_len - split);
    size_t m = telnet_filter_in(buf, in_len - split);
    memcpy(got + n, buf, m);
    n += m;
    if (n != want_len || memcmp(got, want, n) != 0)
    {
        printf("FAIL: %s (split at %zu)\n", what, split);
        failures++;
    }
}

#define CHECK(what, in, want) \
    for (size_t s = 0; s <= sizeof(in) - 1; s++) \
        check(what, (const unsigned char *)in, sizeof(in) - 1, (const unsigned char *)want, sizeof(want) - 1, s)

int main(void)
{
    CHECK("plain bytes", "abc\033[A", "abc\033[A");
    CHECK("IAC IAC is one 0xFF", "a\xff\xff" "b", "a\xff" "b");
    CHECK("WILL/DO dropped", "a\xff\xfb\x01" "b\xff\xfd\x03" "c", "abc");
    CHECK("subnegotiation dropped", "x\xff\xfa\x1f\x00\x50\x00\x19\xff\xf0y", "xy");
    CHECK("IAC IAC inside SB stays inside", "x\xff\xfa\x18\xff\xff\x01\xff\xf0y", "xy");
    CHECK("NOP dropped", "a\xff\xf1" "b", "ab");
    CHECK("CR NUL is CR", "a\r\0b", "a\rb");
    CHECK("CR LF kept", "a\r\nb", "a\r\nb");

    unsigned char out[16];
    size_t n = telnet_escape_out((const unsigned char *)"a\xff" "b", 3, out);
    if (n != 4 || memcmp(out, "a\xff\xff" "b", 4) != 0)
    {
        printf("FAIL: 0xFF doubled going out\n");
        failures++;
    }

    printf(failures ? "%d FAILED\n" : "PASS: telnet layer\n", failures);
    return failures != 0;
}
