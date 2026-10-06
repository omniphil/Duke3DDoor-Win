/*
 * ansi_hud.c -- the game's state for the ANSI mode's text status line. Built with the game's headers and flags
 * (Makefile), since it reads the game's own variables: the player's health, armour, weapon and ammunition,
 * inventory, keycards, and whether a level or a menu is up. And the game's text: the door's own patch
 * (patches/door-text-hooks.patch) calls duke_text_hook from every one of the game's text-drawing functions, and
 * duke_cursor_hook where the menus draw their cursor, so the ANSI mode can draw menus, questions and messages as
 * text (Duke's fonts are pictures, unreadable at 80 columns). (The Wolfenstein door's ansi_hud.c, for Duke.)
 */

#include <pthread.h>
#include <string.h>

#include "duke3d.h"

#include "ansi_hud.h"

/* The frame being drawn (game thread) and the last one finished (read by the door's thread) */
static hud_texts_t     g_building = { .cursor_y = -1000 };
static hud_texts_t     g_shown = { .cursor_y = -1000 };
static pthread_mutex_t g_text_lock = PTHREAD_MUTEX_INITIALIZER;

void duke_text_hook(int kind, int x, int y, int shade, const char *t)
{
    hud_text_t *h;

    if (t == NULL || t[0] == '\0' || g_building.count >= HUD_TEXTS)
        return;
    h = &g_building.texts[g_building.count++];
    h->kind = kind;
    h->x = x;
    h->y = y;
    h->shade = shade;
    strncpy(h->text, t, HUD_TEXT_LEN - 1);
    h->text[HUD_TEXT_LEN - 1] = '\0';
}

void duke_cursor_hook(int x, int y)
{
    (void)x;
    g_building.cursor_y = y;
}

void ansi_hud_frame_done(void)
{
    pthread_mutex_lock(&g_text_lock);
    g_shown = g_building;
    pthread_mutex_unlock(&g_text_lock);
    g_building.count = 0;
    g_building.cursor_y = -1000;
}

void ansi_hud_texts(hud_texts_t *out)
{
    pthread_mutex_lock(&g_text_lock);
    *out = g_shown;
    pthread_mutex_unlock(&g_text_lock);
}

void ansi_hud_read(ansi_hud_t *hud)
{
    const struct player_struct *p = &ps[myconnectindex];
    int item_amount = 0;

    hud->menu = (p->gm & MODE_MENU) != 0;
    hud->in_level = (p->gm & MODE_GAME) != 0 && (p->gm & (MODE_MENU | MODE_EOL | MODE_DEMO)) == 0;
    hud->auto_run = ud.auto_run != 0;
    hud->episode = ud.volume_number + 1;
    hud->level = ud.level_number + 1;
    hud->health = p->i >= 0 && p->i < MAXSPRITES ? sprite[p->i].extra : 0;
    if (hud->health < 0)
        hud->health = 0;
    hud->armor = p->shield_amount;
    hud->weapon = p->curr_weapon;
    /* The detonator shows the pipe bombs left, as the game's status bar does */
    hud->ammo = p->curr_weapon >= 0 && p->curr_weapon < MAX_WEAPONS
              ? p->ammo_amount[p->curr_weapon == HANDREMOTE_WEAPON ? HANDBOMB_WEAPON : p->curr_weapon] : 0;
    hud->item = p->inven_icon;
    switch (p->inven_icon)          /* as the game's own inventory display counts them (game.c) */
    {
    case 1: item_amount = p->firstaid_amount; break;
    case 2: item_amount = (p->steroids_amount + 3) >> 2; break;
    case 3: item_amount = (p->holoduke_amount + 15) / 24; break;
    case 4: item_amount = (p->jetpack_amount + 15) >> 4; break;
    case 5: item_amount = p->heat_amount / 12; break;
    case 6: item_amount = (p->scuba_amount + 63) >> 6; break;
    case 7: item_amount = p->boot_amount >> 1; break;
    default: break;
    }
    hud->item_amount = item_amount;
    hud->blue_key = (p->got_access & 1) != 0;
    hud->red_key = (p->got_access & 2) != 0;
    hud->yellow_key = (p->got_access & 4) != 0;
    hud->kills = p->actors_killed;
    hud->max_kills = p->max_actors_killed;
    hud->secrets = p->secret_rooms;
    hud->max_secrets = p->max_secret_rooms;
}
