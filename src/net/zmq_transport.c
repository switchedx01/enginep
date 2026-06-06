/* =========================================================================
 * zmq_transport.c — ZeroMQ socket management (REP + PUB)
 * ========================================================================= */

#include "zmq_transport.h"
#include "art_cache.h"
#include "hub_state.h"
#include <stdio.h>
#include <string.h>
#include <zmq.h>

int zmq_transport_init(HubContext *hub) {
  hub->zmq.ctx = zmq_ctx_new();
  if (!hub->zmq.ctx)
    return -1;

  hub->zmq.rep = zmq_socket(hub->zmq.ctx, ZMQ_REP);
  if (zmq_bind(hub->zmq.rep, "ipc:///tmp/hub_socket.ipc") != 0) {
    fprintf(stderr, "Failed to bind REP socket\n");
    return -1;
  }

  hub->zmq.pub = zmq_socket(hub->zmq.ctx, ZMQ_PUB);
  if (zmq_bind(hub->zmq.pub, "ipc:///tmp/hub_cmd.ipc") != 0) {
    fprintf(stderr, "Failed to bind PUB socket\n");
    return -1;
  }

  return 0;
}

void zmq_poll_messages(HubContext *hub) {
  if (!hub->zmq.rep) return;
  
  char z_buf[MAX_LOG_LINE];
  memset(z_buf, 0, sizeof(z_buf));

  int rc = zmq_recv(hub->zmq.rep, z_buf, sizeof(z_buf) - 1, ZMQ_DONTWAIT);
  if (rc <= 0)
    return;

  z_buf[rc] = '\0';

  /* Parse title */
  char *t = strstr(z_buf, "title\": \"");
  if (t) {
    t += 9;
    char *end = strchr(t, '\"');
    if (end) {
      int len = end - t;
      strncpy(hub->player.title, t, len);
      hub->player.title[len] = 0;
      hub->player.song_selected = true;
    }
  }

  /* Parse artist */
  char *a = strstr(z_buf, "artist\": \"");
  if (a) {
    a += 10;
    char *end = strchr(a, '\"');
    if (end) {
      int len = end - a;
      strncpy(hub->player.artist, a, len);
      hub->player.artist[len] = 0;
    }
  }

  /* Parse art_path */
  char *art = strstr(z_buf, "art_path\": \"");
  if (art) {
    art += 12;
    char *end = strchr(art, '\"');
    if (end) {
      int len = end - art;
      strncpy(hub->player.art_path, art, len);
      hub->player.art_path[len] = 0;
    }
  }

  /* Parse playback state */
  char *st = strstr(z_buf, "state\": \"");
  if (st)
    hub->player.is_playing = (strncmp(st + 9, "playing", 7) == 0);

  /* Parse theme color */
  char *col = strstr(z_buf, "theme_color\": \"");
  if (col) {
    col += 15;
    if (col[0] == '#') {
      unsigned int r_val, g_val, b_val;
      if (sscanf(col + 1, "%02x%02x%02x", &r_val, &g_val, &b_val) == 3) {
        hub->theme.target_accent.r = r_val;
        hub->theme.target_accent.g = g_val;
        hub->theme.target_accent.b = b_val;
      }
    }
  }

  save_hub_state(hub);
  zmq_send(hub->zmq.rep, "ACK", 3, 0);
}

void send_player_command(HubContext *hub, const char *command) {
  if (!hub->zmq.pub)
    return;
  zmq_send(hub->zmq.pub, command, strlen(command), ZMQ_DONTWAIT);
}

void zmq_transport_shutdown(HubContext *hub) {
  if (hub->zmq.rep)
    zmq_close(hub->zmq.rep);
  if (hub->zmq.pub)
    zmq_close(hub->zmq.pub);
  if (hub->zmq.ctx)
    zmq_ctx_destroy(hub->zmq.ctx);
  hub->zmq.rep = NULL;
  hub->zmq.pub = NULL;
  hub->zmq.ctx = NULL;
}
