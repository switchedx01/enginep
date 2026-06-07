#ifndef HUB_CONTEXT_H
#define HUB_CONTEXT_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "common.h"
#include "widget_system.h"
#include "font_renderer.h"

/* =========================================================================
 * Hub Theme (Material You dynamic color)
 * ========================================================================= */
typedef struct {
  SDL_Color bg;
  SDL_Color top_bar;
  SDL_Color accent;
  SDL_Color target_accent;
  SDL_Color text_main;
  SDL_Color text_dim;
} HubTheme;

/* =========================================================================
 * Search Result
 * ========================================================================= */
typedef struct {
  char title[MAX_SONG_TITLE];
  char artist[MAX_SONG_TITLE];
  char filepath[MAX_PATH_LENGTH];
  char art_path[MAX_PATH_LENGTH];
  float position;
  float duration;
} SearchResult;

typedef struct {
  char path[MAX_PATH_LENGTH];
  SDL_Texture *sharp;
  SDL_Texture *blurred;
} ArtCache;

/* =========================================================================
 * Hub Context (Global Singleton — mirrors Harmony's AppContext)
 * ========================================================================= */
typedef struct {
  /* SDL Core */
  SDL_Window *window;
  SDL_Renderer *renderer;
  bool running;

  /* Theme */
  HubTheme theme;

  /* Sidebar */
  bool sidebar_open;
  float sidebar_anim;

  /* Search */
  char search_query[MAX_SONG_TITLE];
  SearchResult results[HUB_MAX_SEARCH_RESULTS];
  int result_count;
  bool search_active;

  /* Player Telemetry (received via ZMQ) */
  struct {
    char title[MAX_SONG_TITLE];
    char artist[MAX_SONG_TITLE];
    char art_path[MAX_PATH_LENGTH];
    bool is_running;
    bool is_playing;
    bool song_selected;
  } player;

  /* Art Cache */
  ArtCache art_cache;

  /* Font Renderer */
  FontRenderer font_renderer;

  /* Widget System */
  WidgetManager widget_mgr;

  /* Mouse State */
  int mouse_x, mouse_y;

  /* Context Menu State */
  bool context_menu_open;
  int context_menu_x, context_menu_y;
  int context_menu_hovered_idx;
  bool context_menu_submenu_open;
  int context_menu_submenu_hovered_idx;

  /* Process Management */
  pid_t player_pid;

  /* ZMQ Transport */
  struct {
    void *ctx;
    void *rep;
    void *pub;
  } zmq;
} HubContext;

/* Initialization (No Singleton) */
void hub_context_init_defaults(HubContext *hub);

#endif /* HUB_CONTEXT_H */
