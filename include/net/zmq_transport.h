#ifndef ZMQ_TRANSPORT_H
#define ZMQ_TRANSPORT_H

#include "hub_context.h"

/* Initialize ZeroMQ context and sockets (REP + PUB) */
int zmq_transport_init(HubContext *hub);

/* Poll for incoming player telemetry and apply to HubContext */
void zmq_poll_messages(HubContext *hub);

/* Send a command string to the player via PUB socket */
void send_player_command(HubContext *hub, const char *command);

/* Clean shutdown of ZMQ resources */
void zmq_transport_shutdown(HubContext *hub);

#endif /* ZMQ_TRANSPORT_H */
