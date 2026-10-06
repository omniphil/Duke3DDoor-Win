/*
 * ansi_hud.h -- what the ANSI mode shows as text instead of Duke's own status bar, whose digits and icons can't be
 * read at 80 columns. Read straight from the game (ansi_hud.c), which runs in this process in ANSI mode.
 */

#ifndef ANSI_HUD_H
#define ANSI_HUD_H

#include <stdbool.h>

typedef struct
{
    bool in_level;          /* playing a level, rather than the title, a menu, or the tally between levels */
    bool menu;              /* a menu is up: keys go to it as they are */
    bool auto_run;          /* the game's own Auto Run is on (R toggles it in ANSI mode) */
    int  episode, level;    /* 1-based */
    int  health, armor, ammo;
    int  weapon;            /* 0 mighty foot, 1 pistol, 2 shotgun, 3 ripper, 4 RPG, 5 pipe bombs, 6 shrinker,
                               7 devastator, 8 trip bombs, 9 freezer, 10 detonator, 11 expander */
    int  item, item_amount; /* the inventory item chosen (0 none, 1 medkit ... 7 boots) and how much is left */
    bool blue_key, red_key, yellow_key;
    int  kills, max_kills, secrets, max_secrets;
} ansi_hud_t;

/* The game's own text in the last frame it drew (menus, questions, messages), for the ANSI mode to draw as text: its
 * fonts can't be read at 80 columns. Positions are the game's 320x200. */
#define HUD_TEXTS    48
#define HUD_TEXT_LEN 48

typedef struct
{
    int  kind;              /* 0 the menus' big font, 1 the small font (questions, messages), 2 the tiny one */
    int  x, y;              /* as the game gave them: x 160 means centred */
    int  shade;             /* 16 and more: dimmed (a choice that can't be made) */
    char text[HUD_TEXT_LEN];
} hud_text_t;

typedef struct
{
    int        count;
    hud_text_t texts[HUD_TEXTS];
    int        cursor_y;    /* the y of the menu item the game's cursor is on, or -1000 */
} hud_texts_t;

/* A copy of the last frame's text */
void ansi_hud_texts(hud_texts_t *out);

/* The game has finished a frame (ansi_host.c trace_present): its text becomes what ansi_hud_texts gives */
void ansi_hud_frame_done(void);

/* Takes a snapshot of the game. Called from the door's thread while the game runs on its own; a value can be a frame
 * old, which is fine for a status line. */
void ansi_hud_read(ansi_hud_t *hud);

#endif
