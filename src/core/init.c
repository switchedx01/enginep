/* =========================================================================
 * init.c — Consolidated initialization/shutdown (mirrors Harmony's init.c)
 * ========================================================================= */

#include "init.h"
#include "art_cache.h"
#include "font_renderer.h"
#include "hub_state.h"
#include "now_playing.h"
#include "data/layout_manager.h"
#include "zmq_transport.h"
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>

int hub_init(HubContext *hub) {
  /* Force X11 for consistent window positioning */
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return -1;
  }

  hub->window = SDL_CreateWindow("Harmony Hub", SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED, HUB_WINDOW_WIDTH,
                                 HUB_WINDOW_HEIGHT, 0);
  if (!hub->window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return -1;
  }

  hub->renderer = SDL_CreateRenderer(
      hub->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!hub->renderer) {
    fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    return -1;
  }
  SDL_SetRenderDrawBlendMode(hub->renderer, SDL_BLENDMODE_BLEND);

  /* Font System */
  if (font_renderer_init(hub->renderer, &hub->font_renderer) != RESULT_SUCCESS) {
    fprintf(stderr, "Font renderer init failed\n");
    return -1;
  }

  /* ZeroMQ Transport */
  if (zmq_transport_init(hub) != 0) {
    fprintf(stderr, "ZMQ transport init failed\n");
    return -1;
  }

  /* Load persisted state */
  load_hub_state(hub);

  /* Register Widgets via Layout Manager or defaults */
  if (!layout_load(&hub->widget_mgr, "widgets.db", hub->renderer) || hub->widget_mgr.count == 0) {
      now_playing_register(&hub->widget_mgr, hub->renderer);
  }

  hub->running = true;
  return 0;
}

void hub_shutdown(HubContext *hub) {
  /* Save state before teardown */
  save_hub_state(hub);
  layout_save(&hub->widget_mgr, "widgets.db");

  /* Terminate player if still running */
  if (hub->player.is_running) {
    send_player_command(hub, "QUIT");
  }
  if (hub->player_pid > 0) {
    kill(hub->player_pid, SIGTERM);
    waitpid(hub->player_pid, NULL, 0);
  }

  /* Teardown subsystems */
  art_cache_shutdown(&hub->art_cache);
  font_renderer_shutdown(&hub->font_renderer);
  zmq_transport_shutdown(hub);

  /* Destroy widgets */
  for (int i = 0; i < hub->widget_mgr.count; i++) {
    Widget *w = &hub->widget_mgr.widgets[i];
    if (w->render_tex)
      SDL_DestroyTexture(w->render_tex);
    if (w->ops.destroy)
      w->ops.destroy(w, hub);
  }

  if (hub->renderer)
    SDL_DestroyRenderer(hub->renderer);
  if (hub->window)
    SDL_DestroyWindow(hub->window);
  SDL_Quit();
}
