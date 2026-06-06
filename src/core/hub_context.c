/* =========================================================================
 * hub_context.c — HubContext utilities
 * ========================================================================= */

#include "hub_context.h"
#include <stdio.h>
#include <string.h>

void hub_context_init_defaults(HubContext *hub) {
    memset(hub, 0, sizeof(HubContext));
    
    hub->running = true;
    
    /* Default Theme */
    hub->theme.bg = (SDL_Color){18, 18, 18, 255};
    hub->theme.top_bar = (SDL_Color){25, 25, 25, 255};
    hub->theme.accent = (SDL_Color){0, 188, 212, 255};
    hub->theme.target_accent = (SDL_Color){0, 188, 212, 255};
    hub->theme.text_main = (SDL_Color){255, 255, 255, 255};
    hub->theme.text_dim = (SDL_Color){150, 150, 150, 255};
    
    /* Default Player State */
    strncpy(hub->player.title, "No active song", MAX_SONG_TITLE - 1);
    strncpy(hub->player.artist, "Unknown Artist", MAX_SONG_TITLE - 1);
}

/* P10 Assertion Debug Helper */
void tst_debugging(const char *format, const char *file, int line,
                   const char *expr) {
  fprintf(stderr, "ASSERTION FAILED: %s (%s:%d) [%s]\n", format, file, line,
          expr);
}
