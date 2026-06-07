#include "input_handler.h"
#include "process_manager.h"
#include "search.h"
#include "zmq_transport.h"
#include "data/layout_manager.h"
#include "widgets/now_playing.h"
#include "widgets/mini_visualizer.h"
#include <string.h>

void hub_process_events(HubContext *hub) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) {
      hub->running = false;
      return;
    }

    /* Text Input (search bar) */
    if (e.type == SDL_TEXTINPUT) {
      hub->search_active = true;
      strncat(hub->search_query, e.text.text,
              MAX_SONG_TITLE - strlen(hub->search_query) - 1);
      perform_search(hub->search_query, hub->results, &hub->result_count);
    }

    /* Key Down */
    if (e.type == SDL_KEYDOWN) {
      if (e.key.keysym.sym == SDLK_BACKSPACE &&
          strlen(hub->search_query) > 0) {
        hub->search_active = true;
        hub->search_query[strlen(hub->search_query) - 1] = 0;
        perform_search(hub->search_query, hub->results, &hub->result_count);
      }
      if (e.key.keysym.sym == SDLK_ESCAPE) {
        hub->sidebar_open = false;
        hub->context_menu_open = false;
        hub->context_menu_submenu_open = false;
        hub->search_active = false;
      }
      if (e.key.keysym.sym == SDLK_TAB) {
        hub->widget_mgr.is_edit_mode = !hub->widget_mgr.is_edit_mode;
        layout_save(&hub->widget_mgr, "widgets.db");
      }
    }

    /* Right-click to open context menu (intercepted before widgets or other elements) */
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
      hub->context_menu_open = true;
      hub->context_menu_x = e.button.x;
      hub->context_menu_y = e.button.y;
      hub->context_menu_hovered_idx = -1;
      hub->context_menu_submenu_open = false;
      hub->context_menu_submenu_hovered_idx = -1;

      if (hub->context_menu_x + 180 > HUB_WINDOW_WIDTH) {
        hub->context_menu_x = HUB_WINDOW_WIDTH - 190;
      }
      if (hub->context_menu_y + 140 > HUB_WINDOW_HEIGHT) {
        hub->context_menu_y = HUB_WINDOW_HEIGHT - 150;
      }
      continue;
    }

    /* Context Menu Active Interaction */
    if (hub->context_menu_open) {
      if (e.type == SDL_MOUSEMOTION) {
        hub->mouse_x = e.motion.x;
        hub->mouse_y = e.motion.y;

        int cx = hub->context_menu_x;
        int cy = hub->context_menu_y;
        int cw = 180;
        int ch = 140;

        if (hub->mouse_x >= cx && hub->mouse_x < cx + cw &&
            hub->mouse_y >= cy && hub->mouse_y < cy + ch) {
          hub->context_menu_hovered_idx = (hub->mouse_y - cy) / 35;
          if (hub->context_menu_hovered_idx < 0) hub->context_menu_hovered_idx = 0;
          if (hub->context_menu_hovered_idx > 3) hub->context_menu_hovered_idx = 3;

          if (hub->context_menu_hovered_idx == 1) {
            hub->context_menu_submenu_open = true;
          } else {
            hub->context_menu_submenu_open = false;
          }
          hub->context_menu_submenu_hovered_idx = -1;
        } else {
          hub->context_menu_hovered_idx = -1;

          if (hub->context_menu_submenu_open) {
            int sx = cx + cw;
            if (sx + 160 > HUB_WINDOW_WIDTH) {
              sx = cx - 160;
            }
            int sy = cy + 35;
            int sw = 160;
            int sh = 70;

            if (hub->mouse_x >= sx && hub->mouse_x < sx + sw &&
                hub->mouse_y >= sy && hub->mouse_y < sy + sh) {
              hub->context_menu_submenu_hovered_idx = (hub->mouse_y - sy) / 35;
              if (hub->context_menu_submenu_hovered_idx < 0) hub->context_menu_submenu_hovered_idx = 0;
              if (hub->context_menu_submenu_hovered_idx > 1) hub->context_menu_submenu_hovered_idx = 1;
            } else {
              /* Keep open if mouse is bridging the gap between main menu and submenu */
              int min_x = (cx < sx) ? cx : sx;
              int max_x = (cx < sx) ? sx + sw : cx + cw;
              if (hub->mouse_x >= min_x && hub->mouse_x < max_x &&
                  hub->mouse_y >= sy && hub->mouse_y < sy + sh) {
                if (!(hub->mouse_x >= sx && hub->mouse_x < sx + sw)) {
                  hub->context_menu_submenu_hovered_idx = -1;
                }
              } else {
                hub->context_menu_submenu_open = false;
                hub->context_menu_submenu_hovered_idx = -1;
              }
            }
          }
        }
        continue;
      }

      if (e.type == SDL_MOUSEBUTTONDOWN) {
        int cx = hub->context_menu_x;
        int cy = hub->context_menu_y;
        int cw = 180;
        int ch = 140;

        bool clicked_main = (e.button.x >= cx && e.button.x < cx + cw &&
                             e.button.y >= cy && e.button.y < cy + ch);

        bool clicked_sub = false;
        int sx = cx + cw;
        if (sx + 160 > HUB_WINDOW_WIDTH) {
          sx = cx - 160;
        }
        int sy = cy + 35;
        int sw = 160;
        int sh = 70;

        if (hub->context_menu_submenu_open) {
          clicked_sub = (e.button.x >= sx && e.button.x < sx + sw &&
                         e.button.y >= sy && e.button.y < sy + sh);
        }

        if (clicked_main && e.button.button == SDL_BUTTON_LEFT) {
          int idx = (e.button.y - cy) / 35;
          if (idx == 0) {
            hub->widget_mgr.is_edit_mode = !hub->widget_mgr.is_edit_mode;
            layout_save(&hub->widget_mgr, "widgets.db");
            hub->context_menu_open = false;
            hub->context_menu_submenu_open = false;
          } else if (idx == 1) {
            hub->context_menu_submenu_open = !hub->context_menu_submenu_open;
          } else if (idx == 2) {
            while (hub->widget_mgr.count > 0) {
              widget_unregister(&hub->widget_mgr, hub->widget_mgr.widgets[0].name);
            }
            now_playing_register(&hub->widget_mgr, hub->renderer);
            layout_save(&hub->widget_mgr, "widgets.db");
            hub->context_menu_open = false;
            hub->context_menu_submenu_open = false;
          } else if (idx == 3) {
            hub->running = false;
            hub->context_menu_open = false;
            hub->context_menu_submenu_open = false;
          }
        } else if (clicked_sub && e.button.button == SDL_BUTTON_LEFT) {
          int idx = (e.button.y - sy) / 35;
          if (idx == 0) {
            bool found = false;
            for (int i = 0; i < hub->widget_mgr.count; i++) {
              if (strcmp(hub->widget_mgr.widgets[i].name, "music_player") == 0) {
                found = true;
                break;
              }
            }
            if (!found) {
              now_playing_register(&hub->widget_mgr, hub->renderer);
              layout_save(&hub->widget_mgr, "widgets.db");
            }
          } else if (idx == 1) {
            bool found = false;
            for (int i = 0; i < hub->widget_mgr.count; i++) {
              if (strcmp(hub->widget_mgr.widgets[i].name, "mini_visualizer") == 0) {
                found = true;
                break;
              }
            }
            if (!found) {
              mini_visualizer_register(&hub->widget_mgr, hub->renderer);
              layout_save(&hub->widget_mgr, "widgets.db");
            }
          }
          hub->context_menu_open = false;
          hub->context_menu_submenu_open = false;
        } else {
          /* Clicked outside or right-clicked elsewhere */
          if (e.button.button == SDL_BUTTON_RIGHT) {
            hub->context_menu_x = e.button.x;
            hub->context_menu_y = e.button.y;
            hub->context_menu_hovered_idx = -1;
            hub->context_menu_submenu_open = false;
            hub->context_menu_submenu_hovered_idx = -1;

            if (hub->context_menu_x + 180 > HUB_WINDOW_WIDTH) {
              hub->context_menu_x = HUB_WINDOW_WIDTH - 190;
            }
            if (hub->context_menu_y + 140 > HUB_WINDOW_HEIGHT) {
              hub->context_menu_y = HUB_WINDOW_HEIGHT - 150;
            }
          } else {
            hub->context_menu_open = false;
            hub->context_menu_submenu_open = false;
          }
        }
        continue;
      }
    }

    /* Mouse Motion */
    if (e.type == SDL_MOUSEMOTION) {
      hub->mouse_x = e.motion.x;
      hub->mouse_y = e.motion.y;
    }

    /* Let the widget system handle events first */
    if (widget_handle_event_all(&hub->widget_mgr, &e, hub->mouse_x,
                                hub->mouse_y)) {
      continue; /* Event consumed by a widget */
    }

    /* Mouse Button Down (non-widget clicks) */
    if (e.type == SDL_MOUSEBUTTONDOWN) {
      /* Hamburger menu toggle */
      if (e.button.x < 60 && e.button.y < 60)
        hub->sidebar_open = !hub->sidebar_open;

      /* Sidebar: Music Player entry */
      if (hub->sidebar_open && e.button.x < HUB_SIDEBAR_WIDTH &&
          e.button.y > 100 && e.button.y < 150) {
        if (e.button.clicks == 2)
          launch_player_process(hub, NULL, false);
        else if (!hub->player.is_running)
          launch_player_process(hub, NULL, true);
      }

      /* Search bar click */
      bool clicked_search_bar = false;
      if (hub->search_active) {
        if (e.button.x > 80 && e.button.x < HUB_WINDOW_WIDTH - 20 && e.button.y > 10 && e.button.y < 50) {
          clicked_search_bar = true;
        }
      } else {
        if (e.button.x > 250 && e.button.x < 650 && e.button.y > 10 && e.button.y < 50) {
          clicked_search_bar = true;
        }
      }

      if (clicked_search_bar) {
        hub->search_active = true;
        if (hub->search_query[0] == '/' ||
            (hub->search_query[0] == '.' && hub->search_query[1] == '/')) {
          launch_player_process(hub, hub->search_query, true);
          hub->search_query[0] = 0;
          hub->search_active = false;
        }
      }

      /* Search result click */
      bool clicked_search_result = false;
      if (hub->search_active && hub->result_count > 0 && e.button.y > 60) {
        int i = (e.button.y - 80) / 70; /* Using larger rows in the revamp */
        if (i >= 0 && i < hub->result_count) {
          launch_player_process(hub, hub->results[i].filepath, true);
          hub->search_query[0] = 0;
          hub->result_count = 0;
          hub->search_active = false;
          clicked_search_result = true;
        }
      }

      /* Clicked outside active search */
      if (hub->search_active && !clicked_search_bar && !clicked_search_result) {
        hub->search_active = false;
      }
    }
  }
}
