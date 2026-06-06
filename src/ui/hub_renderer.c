/* =========================================================================
 * hub_renderer.c — Main frame composition (top bar, sidebar, grid)
 * ========================================================================= */

#include "hub_renderer.h"
#include "art_cache.h"
#include "font_renderer.h"
#include "process_manager.h"
#include "widget_system.h"
#include "draw_primitives.h"
#include <string.h>

void hub_update(HubContext *hub) {
  /* Player status polling (every 500ms) */
  static Uint32 last_check = 0;
  Uint32 now = SDL_GetTicks();
  if (now - last_check > 500) {
    update_player_status(hub);
    last_check = now;
  }

  /* Update art cache when song is selected */
  if (hub->player.song_selected) {
    art_cache_update(hub->renderer, &hub->art_cache, hub->player.art_path, &hub->theme);
  }

  /* Smooth color interpolation */
  hub->theme.accent.r +=
      (hub->theme.target_accent.r - hub->theme.accent.r) * 0.05f;
  hub->theme.accent.g +=
      (hub->theme.target_accent.g - hub->theme.accent.g) * 0.05f;
  hub->theme.accent.b +=
      (hub->theme.target_accent.b - hub->theme.accent.b) * 0.05f;

  /* Sidebar animation */
  if (hub->sidebar_open && hub->sidebar_anim < 1.0f)
    hub->sidebar_anim += 0.1f;
  if (!hub->sidebar_open && hub->sidebar_anim > 0.0f)
    hub->sidebar_anim -= 0.1f;

  /* Update all widgets (physics, etc.) */
  widget_update_all(&hub->widget_mgr, 0.016f);
}

static void draw_glass_background(SDL_Renderer *ren, SDL_Texture *blurred, SDL_Rect dst) {
  if (!blurred) return;
  int tw = 0, th = 0;
  if (SDL_QueryTexture(blurred, NULL, NULL, &tw, &th) == 0) {
    SDL_Rect src;
    src.x = (int)(((float)dst.x / HUB_WINDOW_WIDTH) * tw);
    src.y = (int)(((float)dst.y / HUB_WINDOW_HEIGHT) * th);
    src.w = (int)(((float)dst.w / HUB_WINDOW_WIDTH) * tw);
    src.h = (int)(((float)dst.h / HUB_WINDOW_HEIGHT) * th);

    Uint8 old_alpha;
    SDL_GetTextureAlphaMod(blurred, &old_alpha);
    SDL_SetTextureAlphaMod(blurred, 255);

    SDL_RenderCopy(ren, blurred, &src, &dst);

    SDL_SetTextureAlphaMod(blurred, old_alpha);
  }
}

void hub_render(HubContext *hub) {
  SDL_Renderer *ren = hub->renderer;

  /* Clear background */
  SDL_SetRenderDrawColor(ren, hub->theme.bg.r, hub->theme.bg.g,
                         hub->theme.bg.b, 255);
  SDL_RenderClear(ren);

  /* Welcome Header */
  SDL_Color h_col1 = {255, 255, 255, 255};
  SDL_Color h_col2 = hub->theme.accent;
  render_gradient_header(ren, &hub->font_renderer, "Welcome Home", HUB_WINDOW_WIDTH / 2 - 180, 130,
                         h_col1, h_col2);

  /* Render all active widgets */
  if (hub->player.is_running || hub->widget_mgr.is_edit_mode) {
    if (hub->widget_mgr.is_edit_mode) {
      widget_render_grid_guides(&hub->widget_mgr, ren);
      render_text(ren, &hub->font_renderer, "EDIT MODE - Press TAB to exit & save", HUB_WINDOW_WIDTH / 2 - 150, 70, hub->theme.accent);
    }
    widget_render_all(&hub->widget_mgr, ren, hub->mouse_x, hub->mouse_y);
  }

  /* Top Bar (drawn over grid) */
  SDL_SetRenderDrawColor(ren, hub->theme.top_bar.r, hub->theme.top_bar.g,
                         hub->theme.top_bar.b, 255);
  SDL_Rect top = {0, 0, HUB_WINDOW_WIDTH, 60};
  SDL_RenderFillRect(ren, &top);

  /* Hamburger icon */
  SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
  for (int i = 0; i < 3; i++) {
    SDL_Rect l = {20, 20 + i * 10, 30, 3};
    SDL_RenderFillRect(ren, &l);
  }

  /* Search bar */
  SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
  SDL_Rect srch = {250, 10, 400, 40};
  SDL_RenderFillRect(ren, &srch);
  render_text(ren, &hub->font_renderer,
              strlen(hub->search_query) ? hub->search_query
                                        : "Search Harmony...",
              260, 35, hub->theme.text_dim);

  /* Search results dropdown */
  if (hub->result_count > 0) {
    SDL_SetRenderDrawColor(ren, 30, 30, 30, 240);
    SDL_Rect res_bg = {250, 60, 400, hub->result_count * 50};
    SDL_RenderFillRect(ren, &res_bg);
    for (int i = 0; i < hub->result_count; i++)
      render_text(ren, &hub->font_renderer, hub->results[i].title, 265, 95 + i * 50,
                  hub->theme.text_main);
  }

  /* Sidebar */
  if (hub->sidebar_anim > 0.01f) {
    int sx = (int)((hub->sidebar_anim - 1) * HUB_SIDEBAR_WIDTH);
    SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
    SDL_Rect sb = {sx, 0, HUB_SIDEBAR_WIDTH, HUB_WINDOW_HEIGHT};
    SDL_RenderFillRect(ren, &sb);
    render_text(ren, &hub->font_renderer, "Music Player", sx + 30, 120,
                hub->player.is_running ? hub->theme.text_main
                                       : hub->theme.text_dim);
  }

  /* Render Context Menu (Glassmorphic with flyout submenu) */
  if (hub->context_menu_open) {
    SDL_Rect menu_rect = {hub->context_menu_x, hub->context_menu_y, 180, 140};

    /* 1. Backdrop Blur */
    draw_glass_background(ren, hub->art_cache.blurred, menu_rect);

    /* 2. Dark Tint Overlay */
    fill_rounded_rect_hq(ren, menu_rect.x, menu_rect.y, menu_rect.w, menu_rect.h, 12, (SDL_Color){20, 20, 20, 195});

    /* 3. Border Outline */
    SDL_Color accent_border = hub->theme.accent;
    accent_border.a = 150;
    draw_rounded_outline_hq(ren, menu_rect.x, menu_rect.y, menu_rect.w, menu_rect.h, 12, 1, accent_border);

    /* 4. Menu Items */
    const char *items[4] = {
      hub->widget_mgr.is_edit_mode ? "Exit Edit Mode" : "Enter Edit Mode",
      "Add Widget    >",
      "Reset Layout",
      "Quit Hub"
    };

    for (int i = 0; i < 4; i++) {
      int item_y = menu_rect.y + i * 35;
      SDL_Rect item_rect = {menu_rect.x, item_y, menu_rect.w, 35};

      bool is_hovered = (hub->context_menu_hovered_idx == i);
      if (is_hovered) {
        fill_rounded_rect_hq(ren, item_rect.x + 4, item_rect.y + 4, item_rect.w - 8, item_rect.h - 8, 8, (SDL_Color){255, 255, 255, 25});
      }

      SDL_Color text_col = is_hovered ? hub->theme.text_main : hub->theme.text_dim;
      render_text(ren, &hub->font_renderer, items[i], item_rect.x + 15, item_rect.y + 24, text_col);
    }

    /* 5. Submenu (Flyout) */
    if (hub->context_menu_submenu_open) {
      int sx = menu_rect.x + menu_rect.w;
      if (sx + 160 > HUB_WINDOW_WIDTH) {
        sx = menu_rect.x - 160;
      }
      int sy = menu_rect.y + 35; /* Align with "Add Widget" */
      SDL_Rect sub_rect = {sx, sy, 160, 70};

      draw_glass_background(ren, hub->art_cache.blurred, sub_rect);
      fill_rounded_rect_hq(ren, sub_rect.x, sub_rect.y, sub_rect.w, sub_rect.h, 12, (SDL_Color){20, 20, 20, 195});
      draw_rounded_outline_hq(ren, sub_rect.x, sub_rect.y, sub_rect.w, sub_rect.h, 12, 1, accent_border);

      const char *sub_items[2] = {
        "Now Playing",
        "Mini Visualizer"
      };

      for (int i = 0; i < 2; i++) {
        int sub_item_y = sub_rect.y + i * 35;
        SDL_Rect sub_item_rect = {sub_rect.x, sub_item_y, sub_rect.w, 35};

        bool is_sub_hovered = (hub->context_menu_submenu_hovered_idx == i);
        if (is_sub_hovered) {
          fill_rounded_rect_hq(ren, sub_item_rect.x + 4, sub_item_rect.y + 4, sub_item_rect.w - 8, sub_item_rect.h - 8, 8, (SDL_Color){255, 255, 255, 25});
        }

        SDL_Color sub_text_col = is_sub_hovered ? hub->theme.text_main : hub->theme.text_dim;
        render_text(ren, &hub->font_renderer, sub_items[i], sub_item_rect.x + 15, sub_item_rect.y + 24, sub_text_col);
      }
    }
  }

  SDL_RenderPresent(ren);
}
