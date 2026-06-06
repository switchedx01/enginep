/* =========================================================================
 * process_manager.c — Player process lifecycle management
 * ========================================================================= */

#include "process_manager.h"
#include "zmq_transport.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void update_player_status(HubContext *hub) {
  hub->player.is_running =
      (system("pgrep -x harmony_player > /dev/null") == 0);

  /* Reset UI state when player exits */
  if (!hub->player.is_running) {
    hub->player.song_selected = false;
    strcpy(hub->player.title, "No active song");
    strcpy(hub->player.artist, "Unknown Artist");
    hub->player.art_path[0] = 0;
  }
}

void launch_player_process(HubContext *hub, const char *filepath,
                           bool headless) {
  if (hub->player.is_running) {
    if (!headless) {
      send_player_command(hub, "SHOW_GUI");
    }
    if (filepath) {
      char cmd[MAX_PATH_LENGTH + 32];
      snprintf(cmd, sizeof(cmd), "LOAD %s", filepath);
      send_player_command(hub, cmd);
      send_player_command(hub, "PLAY");
    }
    return;
  }

  hub->player_pid = fork();
  if (hub->player_pid == 0) {
    /* Child process */
    const char *workdir =
        "/mnt/mass-storage/Archive/Documents/dev/Harmony_Retooled";
    if (chdir(workdir) != 0) {
      perror("chdir failed");
      exit(1);
    }

    char *args[16];
    int i = 0;
    args[i++] = "./harmony_player";
    if (headless)
      args[i++] = "--headless";
    if (filepath)
      args[i++] = (char *)filepath;
    args[i] = NULL;

    int fd = open("/dev/null", O_WRONLY);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);

    execvp(args[0], args);
    perror("execvp failed");
    exit(1);
  } else if (hub->player_pid < 0) {
    perror("fork failed");
  }
}

void terminate_player(HubContext *hub) {
  if (hub->player.is_running) {
    send_player_command(hub, "QUIT");
  }
  if (hub->player_pid > 0) {
    kill(hub->player_pid, SIGTERM);
    waitpid(hub->player_pid, NULL, 0);
    hub->player_pid = 0;
  }
}
