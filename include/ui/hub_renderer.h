#ifndef HUB_RENDERER_H
#define HUB_RENDERER_H

#include "hub_context.h"

/* Render the complete hub frame (clear, widgets, top bar, sidebar, present) */
void hub_render(HubContext *hub);

/* Update per-frame animations (theme lerp, sidebar anim, widget physics) */
void hub_update(HubContext *hub);

#endif /* HUB_RENDERER_H */
