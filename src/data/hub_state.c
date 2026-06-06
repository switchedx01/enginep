/* =========================================================================
 * hub_state.c — State persistence (JSON save/load)
 * ========================================================================= */

#include "hub_state.h"
#include <stdio.h>
#include <string.h>

void save_hub_state(HubContext *hub) {
  FILE *f = fopen("/tmp/hub_state.json", "w");
  if (f) {
    fprintf(f,
            "{\n  \"title\": \"%s\",\n  \"artist\": \"%s\",\n  \"art_path\": "
            "\"%s\"\n}\n",
            hub->player.title, hub->player.artist, hub->player.art_path);
    fclose(f);
  }
}

void load_hub_state(HubContext *hub) {
  FILE *f = fopen("/tmp/hub_state.json", "r");
  if (!f)
    return;

  char line[MAX_LOG_LINE];
  while (fgets(line, sizeof(line), f)) {
    char *t = strstr(line, "\"title\": \"");
    if (t) {
      t += 10;
      char *end = strchr(t, '\"');
      if (end) {
        int len = end - t;
        strncpy(hub->player.title, t, len);
        hub->player.title[len] = 0;
      }
    }
    char *a = strstr(line, "\"artist\": \"");
    if (a) {
      a += 11;
      char *end = strchr(a, '\"');
      if (end) {
        int len = end - a;
        strncpy(hub->player.artist, a, len);
        hub->player.artist[len] = 0;
      }
    }
    char *art = strstr(line, "\"art_path\": \"");
    if (art) {
      art += 13;
      char *end = strchr(art, '\"');
      if (end) {
        int len = end - art;
        strncpy(hub->player.art_path, art, len);
        hub->player.art_path[len] = 0;
      }
    }
  }
  fclose(f);
}
