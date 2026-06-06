#ifndef ART_CACHE_H
#define ART_CACHE_H

#include <SDL2/SDL.h>
#include "common.h"
#include "hub_context.h"

/* Update the cached album art textures if the path has changed */
void art_cache_update(SDL_Renderer *ren, ArtCache *cache, const char *path, HubTheme *theme);

/* Free all cached art textures */
void art_cache_shutdown(ArtCache *cache);

#endif /* ART_CACHE_H */
