/* =========================================================================
 * main.c — Hub Entry Point (mirrors Harmony's clean main.c)
 * Architecture: Native C + SDL2 + ZeroMQ + SQLite3
 * ========================================================================= */

#include "hub_context.h"
#include "hub_renderer.h"
#include "init.h"
#include "input_handler.h"
#include "zmq_transport.h"
#include <stdlib.h>

int main(void) {
  setenv("HARMONY_ENGINE", "1", 1);
  HubContext hub_instance;
  hub_context_init_defaults(&hub_instance);
  HubContext *hub = &hub_instance;
  
  hub->widget_mgr.global_context = hub; /* Dispense centralized information */

  if (hub_init(hub) != 0)
    return -1;

  while (hub->running) {
    zmq_poll_messages(hub);
    hub_process_events(hub);
    hub_update(hub);
    hub_render(hub);
    SDL_Delay(16);
  }

  hub_shutdown(hub);
  return 0;
}
