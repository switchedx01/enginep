/* =========================================================================
 * font_renderer.c — stb_truetype font atlas + text rendering
 * ========================================================================= */

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "font_renderer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

Result font_renderer_init(SDL_Renderer *ren, FontRenderer *fr) {
  fr->cdata = malloc(sizeof(stbtt_bakedchar) * 96);
  fr->hdata = malloc(sizeof(stbtt_bakedchar) * 96);
  
  long size;
  unsigned char *buf;
  FILE *f = fopen(HUB_FONT_PATH, "rb");
  if (!f)
    return RESULT_ERROR_FILE_IO;

  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  buf = malloc(size);
  fread(buf, 1, size, f);
  fclose(f);

  /* Regular Font (20px) */
  unsigned char *bmp = malloc(512 * 512);
  stbtt_BakeFontBitmap(buf, 0, 20.0, bmp, 512, 512, 32, 96, (stbtt_bakedchar *)fr->cdata);
  fr->font_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                 SDL_TEXTUREACCESS_STATIC, 512, 512);
  uint32_t *px = malloc(512 * 512 * 4);
  for (int i = 0; i < 512 * 512; i++)
    px[i] = (bmp[i] << 24) | 0xFFFFFF;
  SDL_UpdateTexture(fr->font_tex, NULL, px, 512 * 4);
  SDL_SetTextureBlendMode(fr->font_tex, SDL_BLENDMODE_BLEND);
  free(bmp);
  free(px);

  /* Header Font (64px) */
  unsigned char *h_bmp = malloc(1024 * 1024);
  stbtt_BakeFontBitmap(buf, 0, 64.0, h_bmp, 1024, 1024, 32, 96, (stbtt_bakedchar *)fr->hdata);
  fr->hdr_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STATIC, 1024, 1024);
  uint32_t *h_px = malloc(1024 * 1024 * 4);
  for (int i = 0; i < 1024 * 1024; i++)
    h_px[i] = (h_bmp[i] << 24) | 0xFFFFFF;
  SDL_UpdateTexture(fr->hdr_tex, NULL, h_px, 1024 * 4);
  SDL_SetTextureBlendMode(fr->hdr_tex, SDL_BLENDMODE_BLEND);
  free(h_bmp);
  free(h_px);

  free(buf);
  return RESULT_SUCCESS;
}

void font_renderer_shutdown(FontRenderer *fr) {
  if (fr->font_tex)
    SDL_DestroyTexture(fr->font_tex);
  if (fr->hdr_tex)
    SDL_DestroyTexture(fr->hdr_tex);
  if (fr->cdata)
    free(fr->cdata);
  if (fr->hdata)
    free(fr->hdata);
  fr->font_tex = fr->hdr_tex = NULL;
  fr->cdata = fr->hdata = NULL;
}

void render_text_scaled(SDL_Renderer *ren, FontRenderer *fr, const char *text, float x, float y,
                        float scale, SDL_Color col) {
  if (!fr->font_tex || !text)
    return;
  SDL_SetTextureColorMod(fr->font_tex, col.r, col.g, col.b);
  SDL_SetTextureAlphaMod(fr->font_tex, col.a);
  float curr_x = x;
  while (*text) {
    if (*text >= 32 && *text < 128) {
      stbtt_aligned_quad q;
      float next_x = curr_x;
      float next_y = y;
      stbtt_GetBakedQuad((stbtt_bakedchar *)fr->cdata, 512, 512, *text - 32, &next_x, &next_y, &q,
                         1);

      float w = (q.x1 - q.x0) * scale;
      float h = (q.y1 - q.y0) * scale;
      float dx = (q.x0 - curr_x) * scale;
      float dy = (q.y0 - y) * scale;

      SDL_Rect src = {(int)(q.s0 * 512), (int)(q.t0 * 512),
                      (int)((q.s1 - q.s0) * 512),
                      (int)((q.t1 - q.t0) * 512)};
      SDL_Rect dst = {(int)(curr_x + dx), (int)(y + dy), (int)w, (int)h};
      SDL_RenderCopy(ren, fr->font_tex, &src, &dst);

      curr_x += (next_x - curr_x) * scale;
    }
    text++;
  }
}

void render_text(SDL_Renderer *ren, FontRenderer *fr, const char *text, float x, float y,
                 SDL_Color col) {
  render_text_scaled(ren, fr, text, x, y, 1.0f, col);
}

void render_gradient_header(SDL_Renderer *ren, FontRenderer *fr, const char *text, int x, int y,
                            SDL_Color c1, SDL_Color c2) {
  if (!fr->hdr_tex || !text)
    return;
  SDL_SetTextureAlphaMod(fr->hdr_tex, 255);
  int len = strlen(text);
  float curr_x = (float)x;
  float curr_y = (float)y;
  for (int i = 0; i < len; i++) {
    float t = (float)i / (float)fmax(1, len - 1);
    SDL_Color col = {(uint8_t)(c1.r + (c2.r - c1.r) * t),
                     (uint8_t)(c1.g + (c2.g - c1.g) * t),
                     (uint8_t)(c1.b + (c2.b - c1.b) * t), 255};
    if (text[i] >= 32 && text[i] < 128) {
      stbtt_aligned_quad q;
      stbtt_GetBakedQuad((stbtt_bakedchar *)fr->hdata, 1024, 1024, text[i] - 32, &curr_x, &curr_y,
                         &q, 1);

      SDL_Rect src = {(int)(q.s0 * 1024), (int)(q.t0 * 1024),
                      (int)((q.s1 - q.s0) * 1024),
                      (int)((q.t1 - q.t0) * 1024)};
      SDL_Rect dst = {(int)q.x0, (int)q.y0, (int)(q.x1 - q.x0),
                      (int)(q.y1 - q.y0)};

      SDL_SetTextureColorMod(fr->hdr_tex, col.r, col.g, col.b);
      SDL_RenderCopy(ren, fr->hdr_tex, &src, &dst);
    }
  }
}

float get_text_width_scaled(FontRenderer *fr, const char *text, float scale) {
  if (!text) return 0.0f;
  float curr_x = 0.0f;
  while (*text) {
    if (*text >= 32 && *text < 128) {
      stbtt_aligned_quad q;
      float next_x = curr_x, next_y = 0;
      stbtt_GetBakedQuad((stbtt_bakedchar *)fr->cdata, 512, 512, *text - 32, &next_x, &next_y, &q, 1);
      curr_x += (next_x - curr_x) * scale;
    }
    text++;
  }
  return curr_x;
}

float get_text_width_hdr_scaled(FontRenderer *fr, const char *text, float scale) {
  if (!text) return 0.0f;
  float curr_x = 0.0f;
  while (*text) {
    if (*text >= 32 && *text < 128) {
      stbtt_aligned_quad q;
      float next_x = curr_x, next_y = 0;
      stbtt_GetBakedQuad((stbtt_bakedchar *)fr->hdata, 1024, 1024, *text - 32, &next_x, &next_y, &q, 1);
      curr_x += (next_x - curr_x) * scale;
    }
    text++;
  }
  return curr_x;
}
