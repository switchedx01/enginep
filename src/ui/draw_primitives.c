/* =========================================================================
 * draw_primitives.c — Stateless geometric drawing helpers
 * ========================================================================= */

#include "draw_primitives.h"
#include <math.h>
#include <stdlib.h>

void draw_aa_arc(SDL_Renderer *ren, int cx, int cy, int r, int start_ang,
                 int end_ang, int thickness, SDL_Color col) {
  SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
  for (int t = 0; t < thickness; t++) {
    int cur_r = r + t;
    for (int i = start_ang; i <= end_ang; i++) {
      float rad = i * 3.14159f / 180.0f;
      SDL_RenderDrawPoint(ren, cx + (int)(cosf(rad) * cur_r),
                          cy + (int)(sinf(rad) * cur_r));
    }
  }
  for (int i = start_ang; i <= end_ang; i++) {
    float rad = i * 3.14159f / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a / 2);
    SDL_RenderDrawPoint(ren, cx + (int)(c * (r + thickness)),
                        cy + (int)(s * (r + thickness)));
    SDL_RenderDrawPoint(ren, cx + (int)(c * (r - 1)),
                        cy + (int)(s * (r - 1)));
  }
}

void fill_rounded_rect_hq(SDL_Renderer *ren, int x, int y, int w, int h,
                           int r, SDL_Color col) {
  SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
  SDL_Rect rects[3] = {
      {x + r, y, w - 2 * r, h},
      {x, y + r, r, h - 2 * r},
      {x + w - r, y + r, r, h - 2 * r},
  };
  SDL_RenderFillRects(ren, rects, 3);

  for (int i = 0; i < r; i++) {
    for (int j = 0; j < r; j++) {
      float dx = i + 0.5f;
      float dy = j + 0.5f;
      float dist = sqrtf(dx * dx + dy * dy);
      
      if (dist <= r + 0.5f) {
        float alpha_mult = 1.0f;
        if (dist > r - 0.5f) {
          alpha_mult = (r + 0.5f) - dist;
        }
        SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, (Uint8)(col.a * alpha_mult));
        SDL_RenderDrawPoint(ren, x + r - i - 1, y + r - j - 1);
        SDL_RenderDrawPoint(ren, x + w - r + i, y + r - j - 1);
        SDL_RenderDrawPoint(ren, x + r - i - 1, y + h - r + j);
        SDL_RenderDrawPoint(ren, x + w - r + i, y + h - r + j);
      }
    }
  }
}

void draw_rounded_mask_hq(SDL_Renderer *ren, int x, int y, int w, int h,
                           int r, SDL_Color bg_col) {
  for (int i = 0; i < r; i++) {
    for (int j = 0; j < r; j++) {
      float dx = i + 0.5f;
      float dy = j + 0.5f;
      float dist = sqrtf(dx * dx + dy * dy);
      
      if (dist > r - 0.5f) {
        float alpha_mult = dist - (r - 0.5f);
        if (alpha_mult > 1.0f) alpha_mult = 1.0f;
        
        SDL_SetRenderDrawColor(ren, bg_col.r, bg_col.g, bg_col.b, (Uint8)(255 * alpha_mult));
        SDL_RenderDrawPoint(ren, x + r - i - 1, y + r - j - 1);
        SDL_RenderDrawPoint(ren, x + w - r + i, y + r - j - 1);
        SDL_RenderDrawPoint(ren, x + r - i - 1, y + h - r + j);
        SDL_RenderDrawPoint(ren, x + w - r + i, y + h - r + j);
      }
    }
  }
}

void draw_rounded_outline_hq(SDL_Renderer *ren, int x, int y, int w, int h,
                              int r, int thickness, SDL_Color col) {
  SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
  SDL_Rect edges[4] = {
      {x + r, y, w - 2 * r, thickness},
      {x + r, y + h - thickness, w - 2 * r, thickness},
      {x, y + r, thickness, h - 2 * r},
      {x + w - thickness, y + r, thickness, h - 2 * r},
  };
  SDL_RenderFillRects(ren, edges, 4);

  float outer_r = r - 0.5f;
  float inner_r = r - thickness - 0.5f;
  
  for (int i = 0; i <= r; i++) {
    for (int j = 0; j <= r; j++) {
      float dx = i + 0.5f;
      float dy = j + 0.5f;
      float dist = sqrtf(dx * dx + dy * dy);
      
      if (dist > inner_r && dist <= outer_r + 1.0f) {
        float alpha_mult = 1.0f;
        if (dist > outer_r) {
          alpha_mult = outer_r + 1.0f - dist;
        } else if (dist < inner_r + 1.0f) {
          alpha_mult = dist - inner_r;
        }
        
        if (alpha_mult > 0.0f) {
          SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, (Uint8)(col.a * alpha_mult));
          SDL_RenderDrawPoint(ren, x + r - i - 1, y + r - j - 1);
          SDL_RenderDrawPoint(ren, x + w - r + i, y + r - j - 1);
          SDL_RenderDrawPoint(ren, x + r - i - 1, y + h - r + j);
          SDL_RenderDrawPoint(ren, x + w - r + i, y + h - r + j);
        }
      }
    }
  }
}

void draw_play_pause_button(SDL_Renderer *ren, int x, int y, int radius,
                             bool playing, SDL_Color col) {
  draw_rounded_outline_hq(ren, x - radius, y - radius, radius * 2, radius * 2,
                           radius, 3, col);
  SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
  if (!playing) {
    for (int i = -10; i <= 10; i++) {
      SDL_RenderDrawLine(ren, x - 5, y + i, x + 10 - abs(i), y);
    }
  } else {
    SDL_Rect bar1 = {x - 8, y - 10, 5, 20};
    SDL_Rect bar2 = {x + 3, y - 10, 5, 20};
    SDL_RenderFillRect(ren, &bar1);
    SDL_RenderFillRect(ren, &bar2);
  }
}
