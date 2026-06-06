#ifndef FONT_RENDERER_H
#define FONT_RENDERER_H

#include <SDL2/SDL.h>
#include "common.h"

/* Forward declare stbtt_bakedchar to avoid including stb headers here */
struct stbtt_bakedchar;

typedef struct {
    void *cdata; /* Allocated array of 96 stbtt_bakedchar */
    void *hdata; /* Allocated array of 96 stbtt_bakedchar */
    SDL_Texture *font_tex;
    SDL_Texture *hdr_tex;
} FontRenderer;

/* Load font from disk and create atlases */
Result font_renderer_init(SDL_Renderer *ren, FontRenderer *fr);

/* Clean up font textures */
void font_renderer_shutdown(FontRenderer *fr);

/* Standard text rendering (20px atlas) */
void render_text(SDL_Renderer *ren, FontRenderer *fr, const char *text, float x, float y, SDL_Color col);
void render_text_scaled(SDL_Renderer *ren, FontRenderer *fr, const char *text, float x, float y, float scale, SDL_Color col);

/* Large header text with horizontal color gradient (64px atlas) */
void render_gradient_header(SDL_Renderer *ren, FontRenderer *fr, const char *text, int x, int y, SDL_Color c1, SDL_Color c2);

/* Width calculation helpers */
float get_text_width_scaled(FontRenderer *fr, const char *text, float scale);
float get_text_width_hdr_scaled(FontRenderer *fr, const char *text, float scale);

#endif /* FONT_RENDERER_H */
