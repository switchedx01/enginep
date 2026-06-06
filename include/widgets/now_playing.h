#ifndef NOW_PLAYING_H
#define NOW_PLAYING_H

#include "hub_context.h"
#include "widget_system.h"

/* Create and register the music player widget with the widget manager */
void now_playing_register(WidgetManager *mgr, SDL_Renderer *ren);

#endif /* NOW_PLAYING_H */
