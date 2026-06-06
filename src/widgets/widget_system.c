#include "widget_system.h"
#include "data/layout_manager.h"
#include <math.h>
#include <string.h>

Widget *widget_register(WidgetManager *mgr, const char *name, WidgetOps ops,
                        void *user_data) {
  if (mgr->count >= MAX_WIDGETS)
    return NULL;

  Widget *w = &mgr->widgets[mgr->count++];
  memset(w, 0, sizeof(Widget));
  strncpy(w->name, name, sizeof(w->name) - 1);
  w->ops = ops;
  w->user_data = user_data;
  w->active = true;
  w->prev_x = w->x;
  return w;
}

void widget_unregister(WidgetManager *mgr, const char *name) {
  for (int i = 0; i < mgr->count; i++) {
    if (strcmp(mgr->widgets[i].name, name) == 0) {
      if (mgr->widgets[i].ops.destroy)
        mgr->widgets[i].ops.destroy(&mgr->widgets[i], mgr->global_context);
      if (mgr->widgets[i].render_tex)
        SDL_DestroyTexture(mgr->widgets[i].render_tex);
      /* Shift remaining widgets down */
      for (int j = i; j < mgr->count - 1; j++)
        mgr->widgets[j] = mgr->widgets[j + 1];
      mgr->count--;
      layout_save(mgr, "widgets.db");
      return;
    }
  }
}

void widget_apply_snap_physics(Widget *w, float lerp_speed) {
  if (!w->is_dragging) {
    w->x += (w->target_x - w->x) * lerp_speed;
    w->y += (w->target_y - w->y) * lerp_speed;
  }
  w->w += (w->target_w - w->w) * lerp_speed;
  w->h += (w->target_h - w->h) * lerp_speed;
}

void widget_apply_sway_physics(Widget *w) {
  float dx = w->x - w->prev_x;
  w->prev_x = w->x;

  w->target_rotation = w->is_maximized ? 0.0f : dx * 1.8f;
  if (w->target_rotation > 12.0f)
    w->target_rotation = 12.0f;
  if (w->target_rotation < -12.0f)
    w->target_rotation = -12.0f;
  w->rotation += (w->target_rotation - w->rotation) * 0.15f;
}

void widget_update_all(WidgetManager *mgr, float dt) {
  for (int i = 0; i < mgr->count; i++) {
    Widget *w = &mgr->widgets[i];
    if (!w->active)
      continue;

    /* Apply universal physics */
    widget_apply_snap_physics(w, 0.18f);
    widget_apply_sway_physics(w);

    /* Call widget-specific update */
    if (w->ops.update)
      w->ops.update(w, dt, mgr->global_context);
  }
}

void widget_render_all(WidgetManager *mgr, SDL_Renderer *ren, int mx, int my) {
  for (int i = 0; i < mgr->count; i++) {
    Widget *w = &mgr->widgets[i];
    if (!w->active || !w->ops.render)
      continue;

    /* Render to texture for rotation support */
    if (w->render_tex) {
      SDL_SetRenderTarget(ren, w->render_tex);
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
      SDL_RenderClear(ren);

      int off = 100;
      int lmx = (mx - (int)w->x) + off;
      int lmy = (my - (int)w->y) + off;
      w->ops.render(w, ren, lmx, lmy, mgr->global_context);

      SDL_SetRenderTarget(ren, NULL);

      /* Blit rotated texture to screen */
      SDL_Rect src = {off - 50, off - 50, (int)w->w + 100, (int)w->h + 100};
      SDL_Rect dst = {(int)w->x - 50, (int)w->y - 50, src.w, src.h};
      SDL_Point center = {50 + (int)w->w / 2, 50 + (int)w->h / 2};
      SDL_RenderCopyEx(ren, w->render_tex, &src, &dst, (double)w->rotation,
                       &center, SDL_FLIP_NONE);
    } else {
      /* Direct render (no rotation) */
      w->ops.render(w, ren, mx, my, mgr->global_context);
    }
    
    /* Edit Mode Decorators */
    if (mgr->is_edit_mode) {
      SDL_SetRenderDrawColor(ren, 200, 200, 200, 150);
      SDL_Rect out = {(int)w->x, (int)w->y, (int)w->w, (int)w->h};
      SDL_RenderDrawRect(ren, &out);
      
      /* Remove Button (Top Right) */
      SDL_Rect remove_btn = {(int)(w->x + w->w - 30), (int)w->y, 30, 30};
      SDL_SetRenderDrawColor(ren, 200, 50, 50, 200);
      SDL_RenderFillRect(ren, &remove_btn);
      
      /* Resize Handle (Bottom Right) */
      SDL_Rect resize_btn = {(int)(w->x + w->w - 20), (int)(w->y + w->h - 20), 20, 20};
      SDL_SetRenderDrawColor(ren, 150, 150, 150, 200);
      SDL_RenderFillRect(ren, &resize_btn);
    }
  }
}

bool widget_handle_event_all(WidgetManager *mgr, SDL_Event *e, int mx,
                             int my) {
  /* Handle mouse-up globally first (release drag/resize for any widget) */
  if (e->type == SDL_MOUSEBUTTONUP) {
    for (int i = 0; i < mgr->count; i++) {
      Widget *w = &mgr->widgets[i];
      if (w->is_resizing) {
        w->is_resizing = false;
        /* Snap to stages */
        if (w->w > 630)
          w->target_w = 860;
        else if (w->w > 280)
          w->target_w = 400;
        else
          w->target_w = 160;
        return true;
      }
      if (w->is_dragging) {
        w->is_dragging = false;
        /* Snap back to home slot */
        w->target_x = 20;
        w->target_y = 180;
        return true;
      }
    }
  }

  /* Handle mouse motion for active drag/resize */
  if (e->type == SDL_MOUSEMOTION) {
    for (int i = 0; i < mgr->count; i++) {
      Widget *w = &mgr->widgets[i];

      /* Hover detection */
      w->is_hovered =
          (mx > w->x && mx < w->x + w->w && my > w->y && my < w->y + w->h);

      if (w->is_resizing) {
        float new_w = mx - w->x;
        if (new_w < 160)
          new_w = 160;
        if (new_w > 860)
          new_w = 860;
        w->w = new_w;
        w->target_w = new_w;
        return true;
      }
      if (w->is_dragging) {
        w->x = (float)mx - w->drag_off_x;
        w->y = (float)my - w->drag_off_y;
        w->target_x = w->x;
        w->target_y = w->y;
        return true;
      }
    }
  }

  /* Handle mouse-down: check widget-specific handlers, then generic
   * drag/resize */
  if (e->type == SDL_MOUSEBUTTONDOWN) {
    for (int i = 0; i < mgr->count; i++) {
      Widget *w = &mgr->widgets[i];
      if (!w->active)
        continue;

      /* Is the click inside this widget? */
      if (mx > w->x && mx < w->x + w->w && my > w->y && my < w->y + w->h) {
        if (mgr->is_edit_mode) {
          /* Remove button */
          if (mx > w->x + w->w - 30 && my < w->y + 30) {
            widget_unregister(mgr, w->name);
            return true;
          }
          /* Resize handle */
          if (mx > w->x + w->w - 30 && my > w->y + w->h - 30) {
            w->is_resizing = true;
            return true;
          }
        }

        /* Let the widget handle the event first (e.g. play/pause button) */
        if (w->ops.handle_event) {
          int ux = mx, uy = my;
          if (w->rotation != 0.0f) {
            float cx = w->x + w->w / 2.0f;
            float cy = w->y + w->h / 2.0f;
            float rad = -w->rotation * (M_PI / 180.0f);
            float s = sinf(rad);
            float c = cosf(rad);
            float tx = (float)mx - cx;
            float ty = (float)my - cy;
            ux = (int)(tx * c - ty * s + cx);
            uy = (int)(tx * s + ty * c + cy);
          }
          if (w->ops.handle_event(w, e, ux, uy, mgr->global_context))
            return true;
        }

        /* Drag handle (top center bar area) or anywhere in edit mode */
        float drag_cx = w->x + w->w / 2;
        bool in_drag_bar = (mx > drag_cx - 40 && mx < drag_cx + 40 && my > w->y && my < w->y + 30);
        
        if (mgr->is_edit_mode || in_drag_bar) {
          w->is_dragging = true;
          w->drag_off_x = mx - w->x;
          w->drag_off_y = my - w->y;
          return true;
        }
      }
    }
  }

  return false;
}

/* =========================================================================
 * Grid Drop Guides (+ icons at grid corners while dragging)
 * ========================================================================= */

#define WS_GRID_ORIGIN_X 20
#define WS_GRID_ORIGIN_Y 180
#define WS_GRID_COLS 5
#define WS_GRID_ROWS 3
#define WS_GRID_CELL_W 172  /* (860 / 5) */
#define WS_GRID_CELL_H 130  /* ~(420 / 3) usable vertical space */
#define WS_GRID_FADE_RADIUS 400.0f
#define WS_PLUS_ARM_LEN 7
#define WS_PLUS_THICKNESS 2

void widget_render_grid_guides(WidgetManager *mgr, SDL_Renderer *ren) {
  /* Find any widget currently being dragged */
  Widget *dragged = NULL;
  for (int i = 0; i < mgr->count; i++) {
    if (mgr->widgets[i].is_dragging) {
      dragged = &mgr->widgets[i];
      break;
    }
  }
  if (!dragged && !mgr->is_edit_mode)
    return;

  float wcx = dragged ? (dragged->x + dragged->w / 2.0f) : -1000.0f;
  float wcy = dragged ? (dragged->y + dragged->h / 2.0f) : -1000.0f;

  for (int row = 0; row <= WS_GRID_ROWS; row++) {
    for (int col = 0; col <= WS_GRID_COLS; col++) {
      int gx = WS_GRID_ORIGIN_X + col * WS_GRID_CELL_W;
      int gy = WS_GRID_ORIGIN_Y + row * WS_GRID_CELL_H;

      float dist = 0.0f;
      if (dragged) {
        float dx = (float)gx - wcx;
        float dy = (float)gy - wcy;
        dist = sqrtf(dx * dx + dy * dy);
      }

      float t = dist / WS_GRID_FADE_RADIUS;
      if (t >= 1.0f)
        continue;

      /* Ease-out for smoother falloff */
      float alpha_f = (1.0f - t) * (1.0f - t);
      uint8_t alpha = (uint8_t)(alpha_f * 120.0f);
      if (alpha < 5)
        continue;

      /* Draw + in accent-ish white */
      SDL_SetRenderDrawColor(ren, 200, 200, 200, alpha);
      SDL_Rect h_bar = {gx - WS_PLUS_ARM_LEN, gy - WS_PLUS_THICKNESS / 2,
                        WS_PLUS_ARM_LEN * 2 + 1, WS_PLUS_THICKNESS};
      SDL_RenderFillRect(ren, &h_bar);

      SDL_Rect v_bar = {gx - WS_PLUS_THICKNESS / 2, gy - WS_PLUS_ARM_LEN,
                        WS_PLUS_THICKNESS, WS_PLUS_ARM_LEN * 2 + 1};
      SDL_RenderFillRect(ren, &v_bar);

      /* Soft circular glow */
      uint8_t glow_alpha = (uint8_t)(alpha_f * 30.0f);
      if (glow_alpha > 2) {
        SDL_SetRenderDrawColor(ren, 200, 200, 200, glow_alpha);
        int glow_r = WS_PLUS_ARM_LEN + 4;
        for (int a = 0; a < 360; a += 6) {
          float rad = a * 3.14159f / 180.0f;
          SDL_RenderDrawPoint(ren, gx + (int)(cosf(rad) * glow_r),
                              gy + (int)(sinf(rad) * glow_r));
        }
      }
    }
  }
}
