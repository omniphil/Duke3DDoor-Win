/*
 * ansi_play.c -- playing Duke Nukem 3D in ANSI, for callers without TRACE. (The Wolfenstein door's ansi_play.c, with
 * Duke's status bar, and the caller reached through door.h / platform.h so it runs on Windows too.)
 *
 * The screen, 80x24:
 *   rows 1-22   the picture (ansi_screen.c), the whole view: the game's own status bar is switched off
 *               (ansi_host.c), its digits can't be read at this size. The game's text (menus, questions, messages)
 *               is drawn over it as text where the game put it, the menu item its cursor is on highlighted: Duke's
 *               fonts are pictures too (ansi_hud.c)
 *   row 23      the status bar as text: level, health, armour, weapon and ammunition, inventory, keycards
 *   row 24      the controls, and frames a second when \ is pressed. Its last cell is never written.
 *
 * The loop reads keys, lets go of keys whose time is up, and sends a frame when there is a new one and the link has
 * room for it. If the link is slow, frames are skipped rather than queued, so the picture never falls behind the
 * game: a slow connection gets fewer frames, not old ones. Room is judged by the caller's terminal answering each
 * frame as it arrives (ansi_pace.c): only this machine's output queue can be seen from here, and frames piled up past
 * it, in the BBS and the network, arrived late and in bursts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ansi_host.h"
#include "ansi_hud.h"
#include "ansi_input.h"
#include "ansi_pace.h"
#include "ansi_play.h"
#include "door.h"
#include "platform.h"
#include "trace_duke.h"

#define PICTURE_LAST_ROW 22
#define HUD_ROW          23
#define MESSAGE_ROW      24

#define MAX_FPS          30           /* the game moves 30 times a second */
#define OUTQ_LIMIT       4096         /* send the next frame only once the link has nearly caught up */
#define KEY_QUIT         17           /* Ctrl-Q: back to the BBS at once */
#define KEY_STATS        '\\'

/* ---- the text status bar ---- */

static int put(int row, int col, const char *text, int fg)
{
    ansi_screen_text(row, col, text, fg, C_BLACK);
    return col + (int)strlen(text);
}

static int put_number(int row, int col, const char *format, int value, int fg)
{
    char text[16];
    snprintf(text, sizeof(text), format, value);
    return put(row, col, text, fg);
}

static void draw_status(const ansi_hud_t *hud)
{
    static const char *const WEAPON[12] = { "FOOT", "PISTOL", "SHOTGUN", "RIPPER", "RPG", "PIPEBOMB", "SHRINKER",
                                            "DEVASTATOR", "TRIPBOMB", "FREEZER", "DETONATOR", "EXPANDER" };
    static const char *const ITEM[8] = { "", "MEDKIT", "STEROIDS", "HOLODUKE", "JETPACK", "NIGHTVIS", "SCUBA",
                                         "BOOTS" };
    int col = 1;

    ansi_screen_clear_to_eol(HUD_ROW, 1, C_BLACK);
    if (!hud->in_level)
    {
        col = put(HUD_ROW, col, " DUKE NUKEM 3D", C_YELLOW);
        put(HUD_ROW, col, "   Arrows: choose  Enter: select  Esc: back  Ctrl-Q: to the BBS", C_GREY);
        return;
    }

    col = put(HUD_ROW, col, " E", C_GREY);
    col = put_number(HUD_ROW, col, "%d", hud->episode, C_WHITE);
    col = put(HUD_ROW, col, "L", C_GREY);
    col = put_number(HUD_ROW, col, "%d", hud->level, C_WHITE);
    col = put(HUD_ROW, col, "  HEALTH ", C_GREY);
    col = put_number(HUD_ROW, col, "%d", hud->health, hud->health <= 25 ? C_LRED : C_WHITE);
    col = put(HUD_ROW, col, "  ARMOR ", C_GREY);
    col = put_number(HUD_ROW, col, "%d", hud->armor, C_WHITE);
    col = put(HUD_ROW, col, "  ", C_GREY);
    col = put(HUD_ROW, col, hud->weapon >= 0 && hud->weapon < 12 ? WEAPON[hud->weapon] : "", C_YELLOW);
    if (hud->weapon > 0)
    {
        col = put(HUD_ROW, col, " ", C_GREY);
        col = put_number(HUD_ROW, col, "%d", hud->ammo, hud->ammo == 0 ? C_LRED : C_WHITE);
    }
    if (hud->item > 0 && hud->item < 8)
    {
        col = put(HUD_ROW, col, "  ", C_GREY);
        col = put(HUD_ROW, col, ITEM[hud->item], C_LCYAN);
        col = put_number(HUD_ROW, col, " %d%%", hud->item_amount, C_WHITE);
    }
    col = put(HUD_ROW, col, "  KEYS ", C_GREY);
    col = put(HUD_ROW, col, hud->blue_key ? "\xFE" : "\xFA", hud->blue_key ? C_LBLUE : C_DARKGREY);
    col = put(HUD_ROW, col, hud->red_key ? "\xFE" : "\xFA", hud->red_key ? C_LRED : C_DARKGREY);
    put(HUD_ROW, col, hud->yellow_key ? "\xFE" : "\xFA", hud->yellow_key ? C_YELLOW : C_DARKGREY);
}

/* The game's own text, where it drew it: 320x200 to 80 columns and PICTURE_LAST_ROW rows. The y the game gives is
 * where each font's letters sit differently: the big menu font is drawn 12 above it, the others below. */
static void draw_texts(void)
{
    static hud_texts_t t;

    ansi_hud_texts(&t);
    for (int i = 0; i < t.count; i++)
    {
        const hud_text_t *h = &t.texts[i];
        char line[HUD_TEXT_LEN + 3];
        int centre_y = h->kind == 0 ? h->y - 4 : h->y + 4;
        int row = 1 + centre_y * PICTURE_LAST_ROW / 200;
        int len, col, fg, bg = C_BLACK;
        bool selected = t.cursor_y > -1000 && h->y - t.cursor_y >= -3 && h->y - t.cursor_y <= 3;

        if (row < 1 || row > PICTURE_LAST_ROW)
            continue;
        snprintf(line, sizeof(line), " %s ", h->text);
        for (char *c = line; *c; c++)
            if (*c >= 'a' && *c <= 'z' && h->kind != 2)
                *c = (char)(*c - 'a' + 'A');       /* the game's fonts are capitals */
        len = (int)strlen(line);
        col = h->x == 160 && h->kind != 2 ? 41 - len / 2 : 1 + h->x * SCREEN_COLS / 320 - 1;
        if (col < 1)
            col = 1;
        if (selected)
        {
            fg = C_WHITE;
            bg = C_RED;
        }
        else if (h->shade >= 16)
            fg = C_DARKGREY;
        else
            fg = h->kind == 0 ? C_YELLOW : h->kind == 1 ? C_WHITE : C_GREY;
        ansi_screen_text(row, col, line, fg, bg);
    }
}

static void draw_message_row(const ansi_hud_t *hud, const char *stats)
{
    int col;

    ansi_screen_clear_to_eol(MESSAGE_ROW, 1, C_BLACK);
    col = put(MESSAGE_ROW, 2, "Arrows move  ,. strafe  F fire  Space open  A jump  Esc menu  ", C_DARKGREY);
    put(MESSAGE_ROW, col, hud->auto_run ? "R: RUN" : "R: walk", hud->auto_run ? C_LCYAN : C_DARKGREY);
    if (stats[0])
        put(MESSAGE_ROW, SCREEN_COLS - (int)strlen(stats), stats, C_LCYAN);   /* ends in column 79 */
}

/* ---- the loop ---- */

play_result_t ansi_play(ansi_mode_t mode, bool utf8)
{
    uint32_t *frame = NULL;
    int frame_w = 0, frame_h = 0;
    unsigned frame_seq = 0;
    long last_sent = 0, stats_since;
    long frames = 0, bytes = 0;
    char stats[48] = "";
    bool show_stats = false;
    play_result_t result = PLAY_QUIT;
    ansi_hud_t hud;

    /* Colours reset, screen cleared, cursor hidden, and no wrapping at the right edge */
    door_write("\033[0m\033[2J\033[H\033[?25l\033[?7l");
    ansi_screen_init(mode, utf8);

    if (!ansi_host_start(trace_duke_pak_data(), trace_duke_pak_size(), trace_duke_pak_hash()))
    {
        door_write("\033[0m\033[?7h\033[?25h");
        return PLAY_FAILED;
    }

    ansi_pace_reset();
    stats_since = plat_now_ms();
    for (;;)
    {
        long now;
        int key;

        /* Keys: wait a few milliseconds for some, which is also what paces this loop */
        {
            unsigned char buf[256], keys[256 + ANSI_PACE_HELD];
            int n = door_read_raw(buf, sizeof(buf), 4);
            if (n < 0)
            {
                result = PLAY_HANGUP;
                break;
            }
            if (n > 0)
            {
                long at = plat_now_ms();
                ansi_input_feed(keys, ansi_pace_take(buf, n, keys, at), at);
            }
        }
        now = plat_now_ms();
        {
            unsigned char held[ANSI_PACE_HELD];
            int n = ansi_pace_stale(held, now);
            if (n > 0)
                ansi_input_feed(held, n, now);
        }

        ansi_hud_read(&hud);

        while ((key = ansi_input_next(now)) >= 0)
        {
            if (key == KEY_QUIT)
            {
                result = PLAY_LEFT;
                goto done;
            }
            if (key == KEY_STATS && !hud.menu)
            {
                show_stats = !show_stats;
                stats[0] = '\0';
                continue;
            }
            ansi_input_key(key, hud.menu, now);
        }
        ansi_input_release_due(now);

        if (ansi_host_finished())
            break;
        if (door_time_remaining() <= 0)
        {
            result = PLAY_LEFT;
            break;
        }

        /* A frame, when there's a new one, it's time, and the link has caught up */
        if (now - last_sent >= 1000 / MAX_FPS && plat_out_queued() < OUTQ_LIMIT && ansi_pace_open(now))
        {
            const char *out;
            size_t len;

            if (ansi_host_frame(&frame, &frame_w, &frame_h, &frame_seq))
            {
                ansi_screen_picture(frame, frame_w, frame_h, 1, PICTURE_LAST_ROW);
                draw_texts();
            }
            draw_status(&hud);
            draw_message_row(&hud, stats);

            len = ansi_screen_update(&out);
            if (len > 0)
            {
                door_write_raw(out, len);
                door_write(ANSI_PACE_QUESTION);
                if (door_hung_up())
                {
                    result = PLAY_HANGUP;
                    break;
                }
                ansi_pace_sent(now, len + sizeof(ANSI_PACE_QUESTION) - 1);
                frames++;
                bytes += (long)len;
            }
            last_sent = now;
        }

        /* Frames and bytes a second, shown with \ for judging a connection */
        if (now - stats_since >= 1000)
        {
            if (show_stats)
                snprintf(stats, sizeof(stats), "%ld fps %ld KB/s %ld ms", frames * 1000 / (now - stats_since),
                         bytes * 1000 / (now - stats_since) / 1024, ansi_pace_rtt());
            frames = bytes = 0;
            stats_since = now;
        }
    }

done:
    ansi_input_release_all();
    if (result == PLAY_LEFT)
        ansi_host_stop(3000);           /* the game saves its settings on the way out */
    free(frame);
    door_write("\033[0m\033[?7h\033[?25h\033[2J\033[H");
    return result;
}
