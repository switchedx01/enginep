#ifndef DRAW_PRIMITIVES_H
#define DRAW_PRIMITIVES_H

#include <SDL2/SDL.h>
#include <stdbool.h>

/* Draw an anti-aliased arc segment */
void draw_aa_arc(SDL_Renderer *ren, int cx, int cy, int r, int start_ang,
                 int end_ang, int thickness, SDL_Color col);

/* Fill a rectangle with high-quality rounded corners */
void fill_rounded_rect_hq(SDL_Renderer *ren, int x, int y, int w, int h,
                           int r, SDL_Color col);

/* Draw a rounded corner mask (for clipping art within rounded cards) */
void draw_rounded_mask_hq(SDL_Renderer *ren, int x, int y, int w, int h,
                           int r, SDL_Color bg_col);

/* Draw a rounded rectangle outline with configurable thickness */
void draw_rounded_outline_hq(SDL_Renderer *ren, int x, int y, int w, int h,
                              int r, int thickness, SDL_Color col);

/* Draw a circular play/pause toggle button */
void draw_play_pause_button(SDL_Renderer *ren, int x, int y, int radius,
                             bool playing, SDL_Color col);

#endif /* DRAW_PRIMITIVES_H */
