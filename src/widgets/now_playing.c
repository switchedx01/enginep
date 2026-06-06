/* =========================================================================
 * now_playing.c — Music player widget implementation
 * ========================================================================= */

#include "now_playing.h"
#include "art_cache.h"
#include "draw_primitives.h"
#include "font_renderer.h"
#include "hub_context.h"
#include "zmq_transport.h"

/* ---- Constants ---- */
#define CORNER_RADIUS 24
#define BORDER_THICKNESS 4
#define ART_MARGIN 25

/* ---- Helpers (SRP & Size Rules) ---- */

static void draw_widget_background(SDL_Renderer *ren, SDL_Rect card, HubContext *hub) {
  fill_rounded_rect_hq(ren, card.x, card.y, card.w, card.h, CORNER_RADIUS, (SDL_Color){25, 25, 25, 255});
  
  SDL_RenderSetClipRect(ren, &card);
  SDL_Texture *blurred = hub->art_cache.blurred;
  if (blurred) {
    SDL_RenderCopy(ren, blurred, NULL, &card);
  }
  
  draw_rounded_mask_hq(ren, card.x, card.y, card.w, card.h, CORNER_RADIUS, hub->theme.bg);
  draw_rounded_outline_hq(ren, card.x, card.y, card.w, card.h, CORNER_RADIUS, BORDER_THICKNESS, hub->theme.accent);
}

static void draw_album_art_and_controls(SDL_Renderer *ren, SDL_Rect art_r, HubContext *hub, bool show_side_controls, int mx, int my) {
  SDL_Texture *sharp = hub->art_cache.sharp;
  if (!sharp) return;
  
  SDL_RenderCopy(ren, sharp, NULL, &art_r);
  
  bool mouse_over_art = (mx >= art_r.x && mx <= art_r.x + art_r.w &&
                         my >= art_r.y && my <= art_r.y + art_r.h);
                         
  if (!show_side_controls && mouse_over_art) {
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 80);
    SDL_RenderFillRect(ren, &art_r);
    SDL_Color btn_col = {255, 255, 255, 220};
    draw_play_pause_button(ren, art_r.x + art_r.w / 2, art_r.y + art_r.h / 2, 30, hub->player.is_playing, btn_col);
  }
}

static void render_marquee(SDL_Renderer *ren, FontRenderer *fr, const char *txt, float tx, float ty, float scale, float avail_w, SDL_Color col) {
  float text_w = get_text_width_scaled(fr, txt, scale);
  if (text_w <= avail_w) {
    render_text_scaled(ren, fr, txt, tx, ty, scale, col);
    return;
  }
  
  SDL_Rect old_clip;
  SDL_bool has_old_clip = SDL_RenderIsClipEnabled(ren);
  if (has_old_clip) SDL_RenderGetClipRect(ren, &old_clip);
  
  SDL_Rect text_clip = {(int)tx, (int)(ty - 50), (int)avail_w, 150};
  SDL_RenderSetClipRect(ren, &text_clip);
  
  float loop_w = text_w + 80.0f;
  float offset = fmodf(SDL_GetTicks() * 0.04f, loop_w);
  render_text_scaled(ren, fr, txt, tx - offset, ty, scale, col);
  render_text_scaled(ren, fr, txt, tx - offset + loop_w, ty, scale, col);
  
  if (has_old_clip) SDL_RenderSetClipRect(ren, &old_clip);
  else SDL_RenderSetClipRect(ren, NULL);
}

static void draw_track_text(SDL_Renderer *ren, SDL_Rect card, HubContext *hub, bool show_side_controls) {
  float text_x = card.x + card.h + 10;
  float text_y_top = card.y + card.h / 2 - 25;
  float text_y_bot = card.y + card.h / 2 + 15;

  uint8_t alpha = (card.w < 350) ? (uint8_t)((card.w - 200) * 255 / 150) : 255;
  SDL_Color main_col = hub->theme.text_main; main_col.a = alpha;
  SDL_Color dim_col = hub->theme.text_dim; dim_col.a = alpha;

  float avail_w = (card.x + card.w) - text_x - 25;
  if (show_side_controls) avail_w -= 100;

  render_marquee(ren, &hub->font_renderer, hub->player.title, text_x, text_y_top, 1.2f, avail_w, main_col);
  render_marquee(ren, &hub->font_renderer, hub->player.artist, text_x, text_y_bot, 1.0f, avail_w, dim_col);
}

static void draw_handles(Widget *self, SDL_Renderer *ren, SDL_Rect card, int mx, int my, HubTheme *theme) {
  /* Drag handle */
  SDL_Rect drag_bar = {card.x + card.w / 2 - 25, card.y + 8, 50, 4};
  bool mouse_near_drag = (mx > card.x + card.w / 2 - 60 && mx < card.x + card.w / 2 + 60 && my > card.y && my < card.y + 40);
  
  if (mouse_near_drag || self->is_dragging) {
    fill_rounded_rect_hq(ren, drag_bar.x, drag_bar.y, drag_bar.w, drag_bar.h, 2, (SDL_Color){150, 150, 150, 200});
  }

  /* Resize handle */
  if (self->is_hovered || self->is_resizing) {
    SDL_Color handle_col = theme->accent;
    handle_col.a = 200;
    fill_rounded_rect_hq(ren, card.x + card.w - 10, card.y + (card.h - 30) / 2, 4, 30, 2, handle_col);
  }
}

/* ---- Main Callbacks ---- */

static void music_render(Widget *self, SDL_Renderer *ren, int mx, int my, void *global_context) {
  HubContext *hub = (HubContext *)global_context;
  SDL_Rect card = {100, 100, (int)self->w, (int)self->h}; /* 100 offset for rotation */

  if (!hub->player.is_running || !hub->player.song_selected) {
    fill_rounded_rect_hq(ren, card.x, card.y, card.w, card.h, CORNER_RADIUS, (SDL_Color){25, 25, 25, 255});
    render_text(ren, &hub->font_renderer, "No active song selected", card.x + 30, card.y + card.h / 2, hub->theme.text_dim);
    draw_rounded_outline_hq(ren, card.x, card.y, card.w, card.h, CORNER_RADIUS, 2, (SDL_Color){40, 40, 40, 255});
    return;
  }

  draw_widget_background(ren, card, hub);

  bool show_text = (card.w > 200);
  bool show_side_controls = (card.w > 600);

  SDL_Rect art_r = {card.x + ART_MARGIN, card.y + ART_MARGIN, card.h - 50, card.h - 50};
  draw_album_art_and_controls(ren, art_r, hub, show_side_controls, mx, my);

  if (show_text) {
    draw_track_text(ren, card, hub, show_side_controls);
  }

  if (show_side_controls) {
    draw_play_pause_button(ren, card.x + card.w - 70, card.y + card.h / 2, 30, hub->player.is_playing, hub->theme.accent);
  }

  draw_handles(self, ren, card, mx, my, &hub->theme);
  SDL_RenderSetClipRect(ren, NULL);
}

static bool music_handle_event(Widget *self, SDL_Event *e, int mx, int my, void *global_context) {
  HubContext *hub = (HubContext *)global_context;

  if (e->type == SDL_MOUSEBUTTONDOWN) {
    int w = (int)self->w, h = (int)self->h;
    bool show_side_controls = (w > 600);
    int btn_x, btn_y;

    if (show_side_controls) {
      btn_x = (int)self->x + w - 70;
      btn_y = (int)self->y + h / 2;
    } else {
      int art_size = h - 50;
      btn_x = (int)self->x + ART_MARGIN + art_size / 2;
      btn_y = (int)self->y + ART_MARGIN + art_size / 2;
    }

    if ((mx - btn_x) * (mx - btn_x) + (my - btn_y) * (my - btn_y) < 35 * 35) {
      hub->player.is_playing = !hub->player.is_playing;
      send_player_command(hub, hub->player.is_playing ? "PLAY" : "PAUSE");
      return true;
    }
  }
  return false;
}

/* ---- Registration ---- */

void now_playing_register(WidgetManager *mgr, SDL_Renderer *ren) {
  WidgetOps ops = {
      .render = music_render,
      .handle_event = music_handle_event,
      .update = NULL,
      .destroy = NULL,
  };

  Widget *w = widget_register(mgr, "music_player", ops, NULL);
  if (!w) return;

  w->x = w->target_x = w->prev_x = 20;
  w->y = w->target_y = 180;
  w->w = w->target_w = 860;
  w->h = w->target_h = 160;

  w->render_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 1200, 1200);
  SDL_SetTextureBlendMode(w->render_tex, SDL_BLENDMODE_BLEND);
}
