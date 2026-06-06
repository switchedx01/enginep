#ifndef HUB_STATE_H
#define HUB_STATE_H

#include "hub_context.h"

/* Save current track info to /tmp/hub_state.json */
void save_hub_state(HubContext *hub);

/* Load last known track info from /tmp/hub_state.json */
void load_hub_state(HubContext *hub);

#endif /* HUB_STATE_H */
