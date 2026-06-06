#include "mini_visualizer.h"
#include "hub_context.h"
#include <math.h>

static void vis_render(Widget *self, SDL_Renderer *ren, int mx, int my, void *global_context) {
    HubContext *hub = (HubContext *)global_context;
    
    SDL_Rect card = {(int)self->x, (int)self->y, (int)self->w, (int)self->h};
    
    /* Background */
    SDL_SetRenderDrawColor(ren, 30, 30, 30, 200);
    SDL_RenderFillRect(ren, &card);
    
    SDL_SetRenderDrawColor(ren, 80, 80, 80, 255);
    SDL_RenderDrawRect(ren, &card);
    
    /* Draw Bars */
    int num_bars = card.w / 10;
    if (num_bars <= 0) return;
    
    float time = SDL_GetTicks() * 0.005f;
    int bar_w = 6;
    int spacing = 10;
    
    SDL_Color accent = hub->theme.accent;
    SDL_SetRenderDrawColor(ren, accent.r, accent.g, accent.b, 255);
    
    for (int i = 0; i < num_bars; i++) {
        /* Simulated wave */
        float val = sinf(time + i * 0.3f) * 0.5f + 0.5f; 
        float val2 = cosf(time * 1.2f + i * 0.1f) * 0.5f + 0.5f;
        float h_factor = (val + val2) * 0.5f;
        
        int bar_h = (int)(h_factor * (card.h - 20));
        if (bar_h < 4) bar_h = 4;
        
        SDL_Rect bar = {card.x + 5 + i * spacing, card.y + card.h - 10 - bar_h, bar_w, bar_h};
        SDL_RenderFillRect(ren, &bar);
    }
    
    /* Edit mode handles will be drawn universally by widget_system.c */
}

static bool vis_handle_event(Widget *self, SDL_Event *e, int mx, int my, void *global_context) {
    // No specific interactions for visualizer yet
    return false;
}

void mini_visualizer_register(WidgetManager *mgr, SDL_Renderer *ren) {
    WidgetOps ops = {
        .render = vis_render,
        .handle_event = vis_handle_event,
        .update = NULL,
        .destroy = NULL
    };
    
    Widget *w = widget_register(mgr, "mini_visualizer", ops, NULL);
    if (!w) return;
    
    w->type = WIDGET_MINI_VISUALIZER;
    w->x = w->target_x = w->prev_x = 400;
    w->y = w->target_y = 180;
    w->w = w->target_w = 200;
    w->h = w->target_h = 100;
}
