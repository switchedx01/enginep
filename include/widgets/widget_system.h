#ifndef WIDGET_SYSTEM_H
#define WIDGET_SYSTEM_H

#include <SDL2/SDL.h>
#include <stdbool.h>

#define MAX_WIDGETS 16

typedef enum {
  WIDGET_NOW_PLAYING = 0,
  WIDGET_MINI_VISUALIZER = 1,
  WIDGET_STATS = 2,
  WIDGET_QUICK_PLAYLISTS = 3
} WidgetType;

typedef struct Widget Widget;

/* Function pointers for widget behavior (virtual table) */
typedef struct {
  void (*render)(Widget *self, SDL_Renderer *ren, int mx, int my, void *global_context);
  bool (*handle_event)(Widget *self, SDL_Event *e, int mx, int my, void *global_context);
  void (*update)(Widget *self, float dt, void *global_context);
  void (*destroy)(Widget *self, void *global_context);
} WidgetOps;

struct Widget {
  /* Identity */
  char name[64];
  WidgetType type;
  bool active;

  /* Geometry */
  float x, y, w, h;
  float target_x, target_y, target_w, target_h;

  /* Interaction */
  bool is_dragging, is_resizing;
  float drag_off_x, drag_off_y;
  bool is_maximized;
  float saved_w, saved_h;
  bool is_hovered;

  /* Physics */
  float rotation, target_rotation;
  float prev_x; /* For sway velocity calculation */

  /* Render-to-texture support */
  SDL_Texture *render_tex;

  /* Virtual table */
  WidgetOps ops;

  /* Widget-specific data (opaque pointer) */
  void *user_data;
};

typedef struct {
  Widget widgets[MAX_WIDGETS];
  int count;
  bool is_edit_mode;
  void *global_context; /* Disperses centralized info to all widgets */
} WidgetManager;

/* Registration & Lifecycle */
Widget *widget_register(WidgetManager *mgr, const char *name, WidgetOps ops,
                        void *user_data);
void widget_unregister(WidgetManager *mgr, const char *name);

/* Frame Dispatch */
void widget_update_all(WidgetManager *mgr, float dt);
void widget_render_all(WidgetManager *mgr, SDL_Renderer *ren, int mx, int my);
bool widget_handle_event_all(WidgetManager *mgr, SDL_Event *e, int mx, int my);

/* Common Physics (shared by all widgets) */
void widget_apply_snap_physics(Widget *w, float lerp_speed);
void widget_apply_sway_physics(Widget *w);

/* Grid Drop Guides (+ icons shown at grid corners during drag) */
void widget_render_grid_guides(WidgetManager *mgr, SDL_Renderer *ren);

#endif /* WIDGET_SYSTEM_H */
