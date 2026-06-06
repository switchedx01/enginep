/* =========================================================================
 * art_cache.c — Album art loading, blurring, and color extraction
 * ========================================================================= */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "art_cache.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void apply_heavy_blur(unsigned char *data, int w, int h, int radius) {
  if (!data || radius <= 0)
    return;
  int size = w * h * 4;
  unsigned char *temp = malloc(size);
  if (!temp)
    return;

  for (int pass = 0; pass < 3; pass++) {
    memcpy(temp, data, size);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        int r = 0, g = 0, b = 0, c = 0;
        for (int nx = x - radius; nx <= x + radius; nx += 2) {
          if (nx >= 0 && nx < w) {
            int i = (y * w + nx) * 4;
            r += temp[i];
            g += temp[i + 1];
            b += temp[i + 2];
            c++;
          }
        }
        int i = (y * w + x) * 4;
        data[i] = r / c;
        data[i + 1] = g / c;
        data[i + 2] = b / c;
      }
    }
    memcpy(temp, data, size);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        int r = 0, g = 0, b = 0, c = 0;
        for (int ny = y - radius; ny <= y + radius; ny += 2) {
          if (ny >= 0 && ny < h) {
            int i = (ny * w + x) * 4;
            r += temp[i];
            g += temp[i + 1];
            b += temp[i + 2];
            c++;
          }
        }
        int i = (y * w + x) * 4;
        data[i] = r / c;
        data[i + 1] = g / c;
        data[i + 2] = b / c;
      }
    }
  }
  free(temp);
}

void art_cache_update(SDL_Renderer *ren, ArtCache *cache, const char *path, HubTheme *theme) {
  if (!path || path[0] == '\0' || strcmp(cache->path, path) == 0)
    return;

  /* Destroy old textures */
  if (cache->sharp)
    SDL_DestroyTexture(cache->sharp);
  if (cache->blurred)
    SDL_DestroyTexture(cache->blurred);
  strncpy(cache->path, path, MAX_PATH_LENGTH - 1);

  int w, h, ch;
  unsigned char *data = stbi_load(path, &w, &h, &ch, 4);
  if (!data) {
    cache->sharp = cache->blurred = NULL;
    return;
  }

  /* Sharp texture */
  SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(
      data, w, h, 32, 4 * w, SDL_PIXELFORMAT_RGBA32);
  cache->sharp = SDL_CreateTextureFromSurface(ren, surf);
  SDL_FreeSurface(surf);

  /* Blurred texture */
  apply_heavy_blur(data, w, h, 30);
  surf = SDL_CreateRGBSurfaceWithFormatFrom(data, w, h, 32, 4 * w,
                                            SDL_PIXELFORMAT_RGBA32);
  cache->blurred = SDL_CreateTextureFromSurface(ren, surf);
  SDL_FreeSurface(surf);
  SDL_SetTextureAlphaMod(cache->blurred, 150);

  /* Extract dominant color for theme accent */
  long r_sum = 0, g_sum = 0, b_sum = 0, pixel_count = w * h;
  for (int i = 0; i < pixel_count * 4; i += 4) {
    r_sum += data[i];
    g_sum += data[i + 1];
    b_sum += data[i + 2];
  }
  if (pixel_count > 0 && theme) {
    theme->target_accent.r = (uint8_t)(r_sum / pixel_count);
    theme->target_accent.g = (uint8_t)(g_sum / pixel_count);
    theme->target_accent.b = (uint8_t)(b_sum / pixel_count);

    if (theme->target_accent.r + theme->target_accent.g +
            theme->target_accent.b <
        200) {
      theme->target_accent.r =
          (uint8_t)fmin(255, theme->target_accent.r * 1.5);
      theme->target_accent.g =
          (uint8_t)fmin(255, theme->target_accent.g * 1.5);
      theme->target_accent.b =
          (uint8_t)fmin(255, theme->target_accent.b * 1.5);
    }
  }

  stbi_image_free(data);
}

void art_cache_shutdown(ArtCache *cache) {
  if (cache->sharp)
    SDL_DestroyTexture(cache->sharp);
  if (cache->blurred)
    SDL_DestroyTexture(cache->blurred);
  cache->sharp = NULL;
  cache->blurred = NULL;
  cache->path[0] = '\0';
}
