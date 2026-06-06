#ifndef INIT_H
#define INIT_H

#include "hub_context.h"

/* Consolidated initialization — sets up SDL, ZMQ, fonts, widgets */
int hub_init(HubContext *hub);

/* Clean shutdown — saves state, destroys resources */
void hub_shutdown(HubContext *hub);

#endif /* INIT_H */
