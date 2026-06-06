#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include "hub_context.h"

/* Check if harmony_player process is currently running */
void update_player_status(HubContext *hub);

/* Fork/exec the player binary (headless or GUI) */
void launch_player_process(HubContext *hub, const char *filepath, bool headless);

/* Terminate the player process gracefully */
void terminate_player(HubContext *hub);

#endif /* PROCESS_MANAGER_H */
