/* =========================================================================
 * hub_gui.c - Central Communication Hub for Harmony Player
 * Architecture: Native C + SDL2 + ZeroMQ + SQLite3
 * Compliance: P10 Safety Rules & Harmony Design Mandates
 * ========================================================================= */

#include <SDL2/SDL.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <zmq.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "/mnt/mass-storage/Archive/Documents/dev/Harmony_Retooled/include/vendor/stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#include "/mnt/mass-storage/Archive/Documents/dev/Harmony_Retooled/include/utils/common.h"
#include "/mnt/mass-storage/Archive/Documents/dev/Harmony_Retooled/include/vendor/stb_image.h"

/* =========================================================================
 * Constants & Configuration
 * ========================================================================= */

#define WINDOW_WIDTH 900
#define WINDOW_HEIGHT 600
static int g_window_width = WINDOW_WIDTH;
static int g_window_height = WINDOW_HEIGHT;
#define SIDEBAR_WIDTH 250
#define FONT_PATH                                                              \
  "/mnt/mass-storage/Archive/Documents/dev/Harmony_Retooled/assets/fonts/"     \
  "Roboto-Regular.ttf"
#define DB_PATH                                                                \
  "/mnt/mass-storage/Archive/Documents/dev/Harmony_Retooled/harmony_v2.db"
#define MAX_RESULTS 5
#define TOP_BAR_HEIGHT 60
#define HEADER_HEIGHT 140
#define GRID_MARGIN 20
#define GRID_CELL_W 180
#define GRID_CELL_H 150

/* =========================================================================
 * Type Definitions
 * ========================================================================= */

typedef struct {
  char title[MAX_SONG_TITLE];
  char artist[MAX_SONG_TITLE];
  char filepath[MAX_PATH_LENGTH];
  char art_path[MAX_PATH_LENGTH];
  float position;
  float duration;
} SearchResult;

typedef struct {
  SDL_Color bg;
  SDL_Color top_bar;
  SDL_Color accent;
  SDL_Color target_accent;
  SDL_Color text_main;
  SDL_Color text_dim;
} HubTheme;

typedef struct {
  char path[MAX_PATH_LENGTH];
  SDL_Texture *sharp;
  SDL_Texture *blurred;
} ArtCache;

typedef struct {
  HubTheme *theme;
  SearchResult *current_track;
  ArtCache *art_cache;
  bool *player_running;
  bool *song_selected;
  bool *is_playing;
  void (*send_command)(const char *);
} GlobalWidgetInfo;

typedef struct {
  float x, y, w, h;
  float target_x, target_y;
  float target_w, target_h;
  int stage; // 1: Full, 2: Medium, 3: Small
  bool is_resizing;
  bool is_dragging;
  float drag_off_x, drag_off_y;
  void (*render)(SDL_Renderer *ren, int x, int y, int w, int h, int mx, int my, GlobalWidgetInfo *info);
  bool active;

  // Physics for sway
  float rotation;
  float target_rotation;

  // Grid Placement
  int grid_col, grid_row;

  // Maximized state
  bool is_maximized;
  float saved_w, saved_h;
  bool was_dragged;
} HubWidget;

/* --- Ethical UI Structures --- */

typedef struct {
  char message[128];
  uint32_t start_time;
  uint32_t duration;
  bool active;
} HubToast;
// Cast server state
static int g_cast_server_sock = -1;
static bool g_cast_active = false;
static char g_cast_url[128] = {0};
static pthread_t g_cast_thread;
static volatile bool g_cast_server_running = false;
static uint32_t g_cast_last_poll = 0;
static bool g_cast_client_connected = false;

// Cast popup state
static bool g_cast_popup_open = false;
static float g_cast_popup_anim = 0.0f;
typedef struct { char name[64]; char ip[32]; uint32_t last_seen; } CastDevice;
static CastDevice g_cast_devices[16];
static int g_cast_device_count = 0;
static bool g_cast_scanning = false;
static uint32_t g_cast_scan_start = 0;

typedef struct {
  char title[64];
  char description[256];
  char confirm_label[32];
  char cancel_label[32];
  void (*on_confirm)(void);
  void (*on_cancel)(void);
  bool active;
  bool danger;
} HubModal;



/* =========================================================================
 * Global State
 * ========================================================================= */

static stbtt_bakedchar g_cdata[96]; // Regular 20px
static stbtt_bakedchar g_hdata[96]; // Header 64px
static SDL_Texture *g_font_tex = NULL;
static SDL_Texture *g_hdr_tex = NULL;
static HubTheme g_theme = {{18, 18, 18, 255},  /* bg */
                           {25, 25, 25, 255},  /* top_bar */
                           {0, 188, 212, 255}, /* accent */
                           {0, 188, 212, 255}, /* target_accent */
                           {255, 255, 255, 255}, {150, 150, 150, 255}};

typedef enum {
    TAB_HOME,
    TAB_MUSIC
} HubTab;

static HubTab g_current_tab = TAB_HOME;
static float g_tab_indicator_y = 75.0f;
static float g_header_transition = 1.0f; // 0.0 to 1.0 (Out -> In)
static char g_display_header[64] = "Welcome Home";
static char g_prev_header[64] = "";
static bool g_is_transitioning = false;

static bool g_sidebar_open = false;
static float g_sidebar_anim = 0.0f;
static char g_search_query[MAX_SONG_TITLE] = "";
static SearchResult g_current_track = {"No active song", "Unknown Artist", "", "", 0.0f, 0.0f};
static bool g_player_running = false;
static bool g_song_selected = false;
static bool g_is_playing = false;
static SearchResult g_results[MAX_RESULTS];
static int g_result_count = 0;
static ArtCache g_art_cache = {"", NULL, NULL};
static void *g_z_cmd_sock = NULL;
static pid_t g_player_pid = 0;

static SDL_Texture *g_widget_tex = NULL;
static SDL_Texture *g_orb_tex = NULL;
// Music Widget State
void render_music_widget(SDL_Renderer *ren, int x, int y, int w, int h, int mx, int my, GlobalWidgetInfo *info);

static HubWidget g_music_widget = {
    .x = GRID_MARGIN,
    .y = TOP_BAR_HEIGHT + HEADER_HEIGHT,
    .w = 860,
    .h = 160,
    .target_x = GRID_MARGIN,
    .target_y = TOP_BAR_HEIGHT + HEADER_HEIGHT,
    .target_w = 860,
    .target_h = 160,
    .stage = 1,
    .is_resizing = false,
    .is_dragging = false,
    .drag_off_x = 0,
    .drag_off_y = 0,
    .render = render_music_widget,
    .active = true,
    .rotation = 0.0f,
    .target_rotation = 0.0f,
    .grid_col = 0,
    .grid_row = 0,
    .is_maximized = false,
    .saved_w = 0.0f,
    .saved_h = 0.0f,
    .was_dragged = false};
static bool g_music_widget_hover = false;
static float g_pill_rotation = 0.0f;
static bool g_pill_hover = false;
static SDL_Texture *g_cd_tex = NULL; // Offscreen circular CD compositing texture

// --- Pill Fullscreen Overlay (completely independent from grid widget) ---
typedef struct {
    bool active;          // Is the overlay alive (animating or showing)?
    bool is_maximized;    // Are we expanding to fullscreen?
    float x, y, w, h;    // Current animated position
    float target_x, target_y, target_w, target_h;
} PillFullscreen;
static PillFullscreen g_pill_fs = {0};
static int g_mouse_x, g_mouse_y;

static bool g_settings_open = false;
static HubToast g_toast = {0};
static HubModal g_modal = {0};

void show_toast(const char *msg, uint32_t duration) {
  strncpy(g_toast.message, msg, 127);
  g_toast.start_time = SDL_GetTicks();
  g_toast.duration = duration;
  g_toast.active = true;
}

void show_modal(const char *title, const char *desc, const char *confirm, const char *cancel, bool danger, void (*on_conf)(void), void (*on_canc)(void));
float get_text_width_scaled(const char *text, float scale);
float get_text_width_hdr_scaled(const char *text, float scale);

void show_modal(const char *title, const char *desc, const char *confirm, const char *cancel, bool danger, void (*on_conf)(void), void (*on_canc)(void)) {
  strncpy(g_modal.title, title, 63);
  strncpy(g_modal.description, desc, 255);
  strncpy(g_modal.confirm_label, confirm, 31);
  strncpy(g_modal.cancel_label, cancel, 31);
  g_modal.danger = danger;
  g_modal.on_confirm = on_conf;
  g_modal.on_cancel = on_canc;
  g_modal.active = true;
}

static bool g_should_quit = false;
void quit_action(void) { 
  if (g_player_pid > 0) kill(g_player_pid, SIGTERM);
  system("pkill -9 harmony_player");
  g_should_quit = true; 
}

void signal_handler(int sig) {
  (void)sig;
  if (g_player_pid > 0) kill(g_player_pid, SIGTERM);
  system("pkill -9 harmony_player");
  exit(0);
}
void cancel_quit(void) { g_modal.active = false; }

void clear_privacy_data(void) {
  unlink("/tmp/hub_state.json");
  // In a real app, this would also clear the DB or cached art
  show_toast("Privacy Data Cleared Successfully", 3000);
}

void send_player_command(const char *command);
void render_text_scaled(SDL_Renderer *ren, const char *text, float x, float y,
                        float scale, SDL_Color col);
void render_text(SDL_Renderer *ren, const char *text, float x, float y,
                 SDL_Color col);

/* =========================================================================
 * Utilities & Helpers
 * ========================================================================= */

void tst_debugging(const char *format, const char *file, int line,
                   const char *expr) {
  fprintf(stderr, "ASSERTION FAILED: %s (%s:%d) [%s]\n", format, file, line,
          expr);
}

static void apply_heavy_blur(unsigned char *data, int w, int h, int radius) {
  if (!data || radius <= 0)
    return;
  int size = w * h * 4;
  unsigned char *temp = malloc(size);
  if (!temp)
    return;

  for (int pass = 0; pass < 3; pass++) {
    memcpy(temp, data, size);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        int r = 0, g = 0, b = 0, c = 0;
        for (int nx = x - radius; nx <= x + radius; nx += 2) {
          if (nx >= 0 && nx < w) {
            int i = (y * w + nx) * 4;
            r += temp[i];
            g += temp[i + 1];
            b += temp[i + 2];
            c++;
          }
        }
        int i = (y * w + x) * 4;
        data[i] = r / c;
        data[i + 1] = g / c;
        data[i + 2] = b / c;
      }
    }
    memcpy(temp, data, size);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        int r = 0, g = 0, b = 0, c = 0;
        for (int ny = y - radius; ny <= y + radius; ny += 2) {
          if (ny >= 0 && ny < h) {
            int i = (ny * w + x) * 4;
            r += temp[i];
            g += temp[i + 1];
            b += temp[i + 2];
            c++;
          }
        }
        int i = (y * w + x) * 4;
        data[i] = r / c;
        data[i + 1] = g / c;
        data[i + 2] = b / c;
      }
    }
  }
  free(temp);
}

void update_art_cache(SDL_Renderer *ren, const char *path) {
  if (!path || path[0] == '\0' || strcmp(g_art_cache.path, path) == 0)
    return;

  if (g_art_cache.sharp)
    SDL_DestroyTexture(g_art_cache.sharp);
  if (g_art_cache.blurred)
    SDL_DestroyTexture(g_art_cache.blurred);
  strncpy(g_art_cache.path, path, MAX_PATH_LENGTH - 1);

  int w, h, ch;
  unsigned char *data = stbi_load(path, &w, &h, &ch, 4);
  if (!data) {
    g_art_cache.sharp = g_art_cache.blurred = NULL;
    return;
  }

  SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(
      data, w, h, 32, 4 * w, SDL_PIXELFORMAT_RGBA32);
  g_art_cache.sharp = SDL_CreateTextureFromSurface(ren, surf);
  SDL_FreeSurface(surf);

  apply_heavy_blur(data, w, h, 30);
  surf = SDL_CreateRGBSurfaceWithFormatFrom(data, w, h, 32, 4 * w,
                                            SDL_PIXELFORMAT_RGBA32);
  g_art_cache.blurred = SDL_CreateTextureFromSurface(ren, surf);
  SDL_FreeSurface(surf);
  SDL_SetTextureAlphaMod(g_art_cache.blurred, 150);

  long r_sum = 0, g_sum = 0, b_sum = 0, pixel_count = w * h;
  for (int i = 0; i < pixel_count * 4; i += 4) {
    r_sum += data[i];
    g_sum += data[i + 1];
    b_sum += data[i + 2];
  }
  if (pixel_count > 0) {
    g_theme.target_accent.r = (uint8_t)(r_sum / pixel_count);
    g_theme.target_accent.g = (uint8_t)(g_sum / pixel_count);
    g_theme.target_accent.b = (uint8_t)(b_sum / pixel_count);

    if (g_theme.target_accent.r + g_theme.target_accent.g +
            g_theme.target_accent.b <
        200) {
      g_theme.target_accent.r =
          (uint8_t)fmin(255, g_theme.target_accent.r * 1.5);
      g_theme.target_accent.g =
          (uint8_t)fmin(255, g_theme.target_accent.g * 1.5);
      g_theme.target_accent.b =
          (uint8_t)fmin(255, g_theme.target_accent.b * 1.5);
    }
  }

  stbi_image_free(data);
}

/* =========================================================================
 * Core Functions
 * ========================================================================= */

void save_hub_state(void) {
  FILE *f = fopen("/tmp/hub_state.json", "w");
  if (f) {
    fprintf(f,
            "{\n  \"title\": \"%s\",\n  \"artist\": \"%s\",\n  \"art_path\": "
            "\"%s\"\n}\n",
            g_current_track.title, g_current_track.artist,
            g_current_track.art_path);
    fclose(f);
  }
}

void load_hub_state(void) {
  FILE *f = fopen("/tmp/hub_state.json", "r");
  if (f) {
    char line[MAX_LOG_LINE];
    while (fgets(line, sizeof(line), f)) {
      char *t = strstr(line, "\"title\": \"");
      if (t) {
        t += 10;
        char *end = strchr(t, '\"');
        if (end) {
          int len = end - t;
          strncpy(g_current_track.title, t, len);
          g_current_track.title[len] = 0;
        }
      }
      char *a = strstr(line, "\"artist\": \"");
      if (a) {
        a += 11;
        char *end = strchr(a, '\"');
        if (end) {
          int len = end - a;
          strncpy(g_current_track.artist, a, len);
          g_current_track.artist[len] = 0;
        }
      }
      char *art = strstr(line, "\"art_path\": \"");
      if (art) {
        art += 13;
        char *end = strchr(art, '\"');
        if (end) {
          int len = end - art;
          strncpy(g_current_track.art_path, art, len);
          g_current_track.art_path[len] = 0;
        }
      }
    }
    fclose(f);
  }
}

void update_player_status(void) {
  if (g_player_pid > 0) {
    int status;
    pid_t result = waitpid(g_player_pid, &status, WNOHANG);
    if (result == g_player_pid) {
      g_player_pid = 0;
      g_player_running = false;
    } else if (result == 0) {
      g_player_running = true;
    } else {
      g_player_pid = 0;
      g_player_running = false;
    }
  } else {
    g_player_running = (system("ps -C harmony_player -o stat= | grep -v Z > /dev/null") == 0);
  }
}

Result perform_search(const char *query) {
  if (!c_assert(query != NULL))
    return RESULT_ERROR_NULL_POINTER;
  if (strlen(query) < 2) {
    g_result_count = 0;
    return RESULT_SUCCESS;
  }

  sqlite3 *db;
  if (sqlite3_open(DB_PATH, &db) != SQLITE_OK)
    return RESULT_ERROR_FILE_IO;

  const char *sql =
      "SELECT t.title, a.name, t.filepath, al.art_filename FROM tracks t "
      "LEFT JOIN artists a ON t.artist_id = a.id "
      "LEFT JOIN albums al ON t.album_id = al.id "
      "WHERE t.title LIKE ? LIMIT ?;";
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  char pattern[MAX_SONG_TITLE + 2];
  snprintf(pattern, sizeof(pattern), "%%%s%%", query);
  sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, MAX_RESULTS);

  g_result_count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && g_result_count < MAX_RESULTS) {
    strncpy(g_results[g_result_count].title,
            (const char *)sqlite3_column_text(stmt, 0), MAX_SONG_TITLE - 1);
    const char *artist = (const char *)sqlite3_column_text(stmt, 1);
    strncpy(g_results[g_result_count].artist, artist ? artist : "Unknown",
            MAX_SONG_TITLE - 1);
    strncpy(g_results[g_result_count].filepath,
            (const char *)sqlite3_column_text(stmt, 2), MAX_PATH_LENGTH - 1);
    const char *art = (const char *)sqlite3_column_text(stmt, 3);
    strncpy(g_results[g_result_count].art_path, art ? art : "",
            MAX_PATH_LENGTH - 1);
    g_result_count++;
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return RESULT_SUCCESS;
}

void launch_player_process(const char *filepath, bool headless) {
  if (g_player_running) {
    if (!headless) {
      send_player_command("SHOW_GUI");
    }

    if (filepath) {
      char cmd[MAX_PATH_LENGTH + 32];
      snprintf(cmd, sizeof(cmd), "LOAD %s", filepath);
      send_player_command(cmd);
      send_player_command("PLAY");
      show_toast("Sending LOAD to active player", 2000);
    }
    return;
  }

  show_toast(headless ? "Launching Background Player..." : "Launching Player UI...", 3000);
  g_player_pid = fork();
  if (g_player_pid == 0) {
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
  } else if (g_player_pid < 0) {
    perror("fork failed");
  }

  // Update local metadata immediately so the UI and Cast server reflect it
  if (filepath) {
    // Find the result in results or search query to get title/artist
    for (int i = 0; i < g_result_count; i++) {
      if (strcmp(g_results[i].filepath, filepath) == 0) {
        g_current_track = g_results[i];
        break;
      }
    }
    // If not found (e.g. from command line), at least set filepath
    if (strcmp(g_current_track.filepath, filepath) != 0) {
      strncpy(g_current_track.filepath, filepath, MAX_PATH_LENGTH - 1);
      strncpy(g_current_track.title, "Loading...", MAX_SONG_TITLE - 1);
    }
    g_song_selected = true;
    g_is_playing = true;
  }
}

void send_player_command(const char *command) {
  if (!g_z_cmd_sock)
    return;
  zmq_send(g_z_cmd_sock, command, strlen(command), ZMQ_DONTWAIT);
}

/* =========================================================================
 * UI Layer (Rendering Helpers)
 * ========================================================================= */

/* =========================================================================
 * Cast Server — Lightweight HTTP server for remote Now Playing view
 * Serves a web page at http://IP:8080 and a JSON API at /api/status
 * ========================================================================= */

static const char *CAST_HTML_TEMPLATE =
"<!DOCTYPE html><html><head><meta charset='utf-8'><title>Harmony - Now Playing</title>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>"
"*{margin:0;padding:0;box-sizing:border-box}"
"body{background:#0a0a0a;color:#fff;font-family:'Outfit','Inter',system-ui,sans-serif;display:flex;justify-content:center;align-items:center;min-height:100vh;overflow:hidden}"
".bg{position:fixed;top:0;left:0;right:0;bottom:0;background:radial-gradient(circle at 50% 50%, var(--ac), #000);opacity:0.15;filter:blur(100px);z-index:-1}"
".c{text-align:center;max-width:480px;width:90%;padding:45px;background:rgba(255,255,255,0.03);backdrop-filter:blur(25px);border-radius:45px;border:1px solid rgba(255,255,255,0.06);box-shadow:0 40px 100px rgba(0,0,0,0.6)}"
".art{width:260px;height:260px;border-radius:35px;background:#161616;margin:0 auto 40px;display:flex;align-items:center;justify-content:center;font-size:80px;box-shadow:0 25px 70px rgba(0,0,0,0.5);position:relative;overflow:hidden;transition:transform 0.7s cubic-bezier(0.19, 1, 0.22, 1);background-size:cover;background-position:center}"
".art:hover{transform:scale(1.04) rotate(2deg)}"
"h1{font-size:34px;margin-bottom:8px;font-weight:800;letter-spacing:-1px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
"p{color:#777;font-size:19px;margin-bottom:40px;font-weight:450}"
".bar{width:100%;height:10px;background:rgba(255,255,255,0.08);border-radius:5px;margin:18px 0;overflow:hidden}"
".fill{height:100%;background:linear-gradient(90deg,var(--ac),#fff);border-radius:5px;transition:width 0.9s cubic-bezier(0.19, 1, 0.22, 1)}"
".time{display:flex;justify-content:space-between;color:#555;font-size:14px;font-weight:700;font-variant-numeric:tabular-nums}"
".cast{display:inline-block;background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.1);padding:14px 35px;border-radius:30px;color:#fff;font-size:15px;margin-top:30px;font-weight:700;cursor:pointer;transition:all 0.4s;letter-spacing:0.5px}"
".cast:hover{background:var(--ac);color:#000;border-color:transparent;transform:translateY(-3px);box-shadow:0 15px 30px rgba(0,0,0,0.3)}"
":root{--ac:#0cf}"
"</style></head><body><div class='bg' id='bg'></div><div class='c'>"
"<div class='art' id='art'>♪</div>"
"<h1 id='title'>Waiting...</h1>"
"<p id='artist'>Harmony Hub</p>"
"<div class='bar'><div class='fill' id='prog' style='width:0%'></div></div>"
"<div class='time'><span id='pos'>0:00</span><span id='dur'>0:00</span></div>"
"<audio id='ap' preload='auto' controls style='width:100%;margin-top:20px;display:none'></audio>"
"<div id='playbtn' class='cast'>START LISTENING</div>"
"<div id='status' style='color:#333;font-size:11px;margin-top:20px;font-weight:800;text-transform:uppercase;letter-spacing:2px'>Connection Stable</div>"
"</div><script>"
"var ap=document.getElementById('ap'); var pb=document.getElementById('playbtn'); var st=document.getElementById('status');"
"var first_load=true; var last_path=''; var last_art='';"
"pb.onclick=()=>{fetch('/api/command?c=PLAY');ap.play();pb.style.display='none';st.textContent='Live Stream Active';st.style.color='var(--ac)';};"
"function fmt(s){s=Math.floor(s);var m=Math.floor(s/60);return m+':'+(s%60<10?'0':'')+s%60}"
"setInterval(()=>fetch('/api/status').then(r=>r.json()).then(d=>{"
"  if(!d) return;"
"  document.getElementById('title').textContent = d.title || 'No Track';"
"  document.getElementById('artist').textContent = d.artist || 'Unknown Artist';"
"  document.getElementById('pos').textContent = fmt(d.position || 0);"
"  document.getElementById('dur').textContent = fmt(d.duration || 0);"
"  var p = (d.duration > 0) ? (d.position / d.duration * 100) : 0;"
"  document.getElementById('prog').style.width = p + '%';"
"  document.querySelector(':root').style.setProperty('--ac', d.color || '#0cf');"
"  document.getElementById('status').textContent = 'Live • ' + (d.playing ? 'Playing' : 'Paused');"
"  document.getElementById('status').style.color = d.playing ? 'var(--ac)' : '#555';"
"  if(d.art_path && d.art_path != last_art){"
"    last_art = d.art_path; document.getElementById('art').style.backgroundImage = 'url(/api/art?path=' + encodeURIComponent(d.art_path) + ')';"
"    document.getElementById('art').textContent = '';"
"  }"
"  if(d.filepath && d.filepath != last_path){"
"    last_path = d.filepath; ap.src = '/api/stream?t=' + Date.now();"
"    pb.style.display = 'inline-block'; first_load = true;"
"  }"
"  if(first_load && d.position > 0){"
"    ap.currentTime = d.position; first_load = false;"
"  }"
"  if(!ap.paused) fetch('/api/sync?pos=' + ap.currentTime);"
"}).catch(e=>{ console.error(e); document.getElementById('status').textContent='Reconnecting...'; }), 1000);"
"</script></body></html>";

static void get_local_ip(char *buf, int buflen) {
  struct ifaddrs *ifaddr, *ifa;
  strncpy(buf, "127.0.0.1", buflen); // Default
  if (getifaddrs(&ifaddr) == -1) return;
  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) continue;
    if (ifa->ifa_addr->sa_family == AF_INET) {
      char *ip = inet_ntoa(((struct sockaddr_in *)ifa->ifa_addr)->sin_addr);
      if (strcmp(ip, "127.0.0.1") != 0) {
        strncpy(buf, ip, buflen);
        break;
      }
    }
  }
  freeifaddrs(ifaddr);
}

static int g_net_discovery_sock = -1;

void init_net_discovery() {
  if (g_net_discovery_sock >= 0) return;
  g_net_discovery_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (g_net_discovery_sock < 0) return;
  int opt = 1;
  setsockopt(g_net_discovery_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  fcntl(g_net_discovery_sock, F_SETFL, O_NONBLOCK);
}

void send_discovery_queries() {
  if (g_net_discovery_sock < 0) init_net_discovery();
  if (g_net_discovery_sock < 0) return;

  // SSDP M-SEARCH for Smart TVs / DIAL
  struct sockaddr_in ssdp_addr = {0};
  ssdp_addr.sin_family = AF_INET;
  ssdp_addr.sin_port = htons(1900);
  inet_pton(AF_INET, "239.255.255.250", &ssdp_addr.sin_addr);
  const char *ssdp_msg = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: ssdp:all\r\n\r\n";
  sendto(g_net_discovery_sock, ssdp_msg, strlen(ssdp_msg), 0, (struct sockaddr*)&ssdp_addr, sizeof(ssdp_addr));

  // mDNS for Google Cast / Chromecasts
  struct sockaddr_in mdns_addr = {0};
  mdns_addr.sin_family = AF_INET;
  mdns_addr.sin_port = htons(5353);
  inet_pton(AF_INET, "224.0.0.251", &mdns_addr.sin_addr);
  unsigned char mdns_query[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0b, '_', 'g', 'o', 'o', 'g', 'l', 'e', 'c', 'a', 's', 't',
    0x04, '_', 't', 'c', 'p', 0x05, 'l', 'o', 'c', 'a', 'l', 0x00,
    0x00, 0x0c, 0x00, 0x01
  };
  sendto(g_net_discovery_sock, mdns_query, sizeof(mdns_query), 0, (struct sockaddr*)&mdns_addr, sizeof(mdns_addr));
}

void poll_net_discovery() {
  if (g_net_discovery_sock < 0) return;
  char buf[4096];
  struct sockaddr_in sender;
  socklen_t slen = sizeof(sender);
  int n;
  while ((n = recvfrom(g_net_discovery_sock, buf, sizeof(buf)-1, 0, (struct sockaddr*)&sender, &slen)) > 0) {
    buf[n] = '\0';
    char *ip = inet_ntoa(sender.sin_addr);
    char name[64] = "Unknown Device";
    
    if (strstr(buf, "_googlecast") || strstr(buf, "fn=") || strstr(buf, "Google")) {
      strcpy(name, "Chromecast / Google Hub");
      char *fn = strstr(buf, "fn=");
      if (fn) {
        fn += 3; char *end = strchr(fn, ',');
        if (!end) end = strchr(fn, '\0');
        int len = end - fn; if (len > 63) len = 63;
        strncpy(name, fn, len); name[len] = '\0';
      }
    } else if (strstr(buf, "HTTP/1.1") || strstr(buf, "LOCATION:")) {
      strcpy(name, "Smart TV / Hub");
    } else continue;

    bool exists = false;
    for (int i=0; i<g_cast_device_count; i++) {
      if (strcmp(g_cast_devices[i].ip, ip) == 0) { exists = true; break; }
    }
    if (!exists && g_cast_device_count < 16) {
      strncpy(g_cast_devices[g_cast_device_count].ip, ip, 31);
      strncpy(g_cast_devices[g_cast_device_count].name, name, 63);
      g_cast_devices[g_cast_device_count].last_seen = SDL_GetTicks();
      g_cast_device_count++;
    }
  }
}

static void *cast_server_thread(void *arg) {
  (void)arg;
  int server_fd = -1;
  int port = 8080;
  
  // Try to bind to 8080, fallback to others if busy
  for (int p = 8080; p < 8085; p++) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) continue;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(p);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
      port = p;
      printf("[Cast] Server bound to port %d\n", port);
      break;
    }
    close(server_fd);
    server_fd = -1;
  }

  if (server_fd < 0) {
    fprintf(stderr, "[Cast] Failed to bind to any port\n");
    g_cast_server_running = false;
    return NULL;
  }

  listen(server_fd, 5);
  g_cast_server_sock = server_fd;
  g_cast_server_running = true;

  // Build URL
  char ip[64] = {0};
  get_local_ip(ip, sizeof(ip));
  snprintf(g_cast_url, sizeof(g_cast_url), "http://%s:%d", ip, port);
  printf("[Cast] URL: %s\n", g_cast_url);

  struct timeval tv = {1, 0};
  while (g_cast_server_running) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(server_fd, &fds);
    int ret = select(server_fd + 1, &fds, NULL, NULL, &tv);
    tv.tv_sec = 1; tv.tv_usec = 0;
    if (ret <= 0) continue;

    struct sockaddr_in client;
    socklen_t clen = sizeof(client);
    int client_fd = accept(server_fd, (struct sockaddr*)&client, &clen);
    if (client_fd < 0) continue;

    char req[2048];
    int n = recv(client_fd, req, sizeof(req)-1, 0);
    if (n <= 0) { close(client_fd); continue; }
    req[n] = '\0';

    char *get_pos = strstr(req, "GET ");
    if (!get_pos) { close(client_fd); continue; }
    char *path = get_pos + 4;

    if (strncmp(path, "/api/status", 11) == 0) {
      g_cast_last_poll = SDL_GetTicks();
      char json[1024];
      char color_hex[8] = "#00ccff";
      snprintf(color_hex, sizeof(color_hex), "#%02x%02x%02x",
               g_theme.accent.r, g_theme.accent.g, g_theme.accent.b);
               
      char esc_path[MAX_PATH_LENGTH] = {0};
      if (g_current_track.filepath[0]) {
        strncpy(esc_path, g_current_track.filepath, MAX_PATH_LENGTH-1);
        for(int i=0; esc_path[i]; i++) if(esc_path[i] == '\\') esc_path[i] = '/';
      }

      snprintf(json, sizeof(json),
        "{\"title\":\"%s\",\"artist\":\"%s\",\"filepath\":\"%s\",\"art_path\":\"%s\",\"position\":%.1f,\"duration\":%.1f,\"playing\":%s,\"color\":\"%s\"}",
        g_current_track.title, g_current_track.artist, esc_path, g_current_track.art_path,
        g_current_track.position, g_current_track.duration,
        g_is_playing ? "true" : "false", color_hex);
      
      char resp[2048];
      snprintf(resp, sizeof(resp),
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\n\r\n%s",
        strlen(json), json);
      send(client_fd, resp, strlen(resp), 0);
    } else if (strncmp(path, "/api/sync", 9) == 0) {
      char *p = strstr(path, "pos=");
      if (p) {
        float new_pos = atof(p + 4);
        if (new_pos >= 0) {
          g_current_track.position = new_pos;
          // Notify the player process to seek if it's running (to keep local state in sync)
          char cmd[64];
          snprintf(cmd, sizeof(cmd), "SEEK %.2f", new_pos);
          send_player_command(cmd);
        }
      }
      const char *ok = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
      send(client_fd, ok, strlen(ok), 0);
    } else if (strncmp(path, "/api/art", 8) == 0) {
      char *p = strstr(path, "path=");
      if (p) {
        p += 5;
        char img_path[MAX_PATH_LENGTH] = {0};
        char *end = strchr(p, ' ');
        if (!end) end = strchr(p, '&');
        if (!end) end = strchr(p, '\r');
        if (end) {
          int len = end - p;
          if (len > MAX_PATH_LENGTH - 1) len = MAX_PATH_LENGTH - 1;
          strncpy(img_path, p, len);
          // Simple URL decode for %2F etc.
          char decoded[MAX_PATH_LENGTH] = {0};
          int j = 0;
          for(int i=0; img_path[i] && j < MAX_PATH_LENGTH-1; i++) {
            if(img_path[i] == '%' && img_path[i+1] && img_path[i+2]) {
              char hex[3] = {img_path[i+1], img_path[i+2], 0};
              decoded[j++] = (char)strtol(hex, NULL, 16);
              i += 2;
            } else decoded[j++] = img_path[i];
          }

          int fd = open(decoded, O_RDONLY);
          if (fd >= 0) {
            struct stat st; fstat(fd, &st);
            char hdr[256];
            snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %ld\r\n\r\n", st.st_size);
            send(client_fd, hdr, strlen(hdr), 0);
            unsigned char fbuf[16384]; ssize_t bytes;
            while ((bytes = read(fd, fbuf, sizeof(fbuf))) > 0) {
              if (send(client_fd, fbuf, bytes, 0) <= 0) break;
            }
            close(fd);
          } else {
            const char *r404 = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(client_fd, r404, strlen(r404), 0);
          }
        }
      }
    } else if (strncmp(path, "/api/command", 12) == 0) {
      char *c = strstr(path, "c=");
      if (c) {
        c += 2;
        char cmd[32] = {0};
        char *end = strchr(c, ' ');
        if (!end) end = strchr(c, '&');
        if (!end) end = strchr(c, '\r');
        if (end) {
          int len = end - c;
          if (len > 31) len = 31;
          strncpy(cmd, c, len);
          send_player_command(cmd);
          if (strcmp(cmd, "PLAY") == 0) g_is_playing = true;
          if (strcmp(cmd, "PAUSE") == 0) g_is_playing = false;
        }
      }
      const char *ok = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
      send(client_fd, ok, strlen(ok), 0);
    } else if (strncmp(path, "/api/stream", 11) == 0) {
      printf("[Cast] Serving stream: %s\n", g_current_track.filepath);
      g_cast_client_connected = true;
      g_cast_last_poll = SDL_GetTicks();
      send_player_command("VOLUME 0"); // Mute local playback when streaming
      int fd = open(g_current_track.filepath, O_RDONLY);
      if (fd >= 0) {
        struct stat st;
        fstat(fd, &st);
        
        // Detect content type from extension
        const char *ctype = "audio/mpeg";
        const char *ext = strrchr(g_current_track.filepath, '.');
        if (ext) {
          if (strcasecmp(ext, ".flac") == 0) ctype = "audio/flac";
          else if (strcasecmp(ext, ".wav") == 0) ctype = "audio/wav";
          else if (strcasecmp(ext, ".ogg") == 0) ctype = "audio/ogg";
          else if (strcasecmp(ext, ".m4a") == 0) ctype = "audio/mp4";
          else if (strcasecmp(ext, ".aac") == 0) ctype = "audio/aac";
        }
        
        char hdr[256];
        snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nAccess-Control-Allow-Origin: *\r\n\r\n", ctype, st.st_size);
        send(client_fd, hdr, strlen(hdr), 0);
        
        unsigned char fbuf[16384];
        ssize_t bytes;
        while ((bytes = read(fd, fbuf, sizeof(fbuf))) > 0) {
          if (send(client_fd, fbuf, bytes, 0) <= 0) break;
        }
        close(fd);
      } else {
        const char *r404 = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, r404, strlen(r404), 0);
      }
    } else if (strncmp(path, "/ ", 2) == 0 || strncmp(path, "/\r", 2) == 0 || strncmp(path, "/ HTTP", 6) == 0) {
      char resp_hdr[256];
      int html_len = strlen(CAST_HTML_TEMPLATE);
      snprintf(resp_hdr, sizeof(resp_hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", html_len);
      send(client_fd, resp_hdr, strlen(resp_hdr), 0);
      send(client_fd, CAST_HTML_TEMPLATE, html_len, 0);
    } else {
      const char *r404 = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
      send(client_fd, r404, strlen(r404), 0);
    }
    close(client_fd);
  }
  close(server_fd);
  return NULL;
}

void start_cast_server(void) {
  if (g_cast_server_running) return;
  // Initialize discovery as well
  init_net_discovery();
  pthread_create(&g_cast_thread, NULL, cast_server_thread, NULL);
}

void stop_cast_server(void) {
  if (!g_cast_server_running) return;
  g_cast_server_running = false;
  pthread_join(g_cast_thread, NULL);
  g_cast_active = false;
  g_cast_url[0] = '\0';
}

// Forward declarations for rendering helpers
void fill_rounded_rect_hq(SDL_Renderer *ren, int x, int y, int w, int h, int r, SDL_Color col);
void draw_rounded_outline_hq(SDL_Renderer *ren, int x, int y, int w, int h, int r, int thickness, SDL_Color col);

void render_cast_popup(SDL_Renderer *ren) {
  if (!g_cast_popup_open && g_cast_popup_anim < 0.01f) return;

  // Animate open/close
  float target = g_cast_popup_open ? 1.0f : 0.0f;
  g_cast_popup_anim += (target - g_cast_popup_anim) * 0.15f;
  if (g_cast_popup_anim < 0.01f) return;

  float a = g_cast_popup_anim;

  // Dim background
  SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)(120 * a));
  SDL_Rect screen = {0, 0, g_window_width, g_window_height};
  SDL_RenderFillRect(ren, &screen);

  int pw = 420, ph = 340;
  int px = (g_window_width - pw) / 2;
  int py = (int)((g_window_height - ph) / 2 + (1.0f - a) * 40);

  // Panel background
  fill_rounded_rect_hq(ren, px, py, pw, ph, 16, (SDL_Color){28, 28, 28, (Uint8)(250 * a)});
  draw_rounded_outline_hq(ren, px, py, pw, ph, 16, 1, (SDL_Color){g_theme.accent.r, g_theme.accent.g, g_theme.accent.b, (Uint8)(180 * a)});

  SDL_Color title_col = {255, 255, 255, (Uint8)(255 * a)};
  SDL_Color dim_col = {150, 150, 150, (Uint8)(200 * a)};
  SDL_Color accent_a = {g_theme.accent.r, g_theme.accent.g, g_theme.accent.b, (Uint8)(255 * a)};

  // Title
  render_text(ren, "Cast", px + 25, py + 38, title_col);

  // Close X
  SDL_SetRenderDrawColor(ren, 150, 150, 150, (Uint8)(200 * a));
  int cx = px + pw - 35, cy = py + 25;
  SDL_RenderDrawLine(ren, cx, cy, cx + 14, cy + 14);
  SDL_RenderDrawLine(ren, cx + 14, cy, cx, cy + 14);

  // Section 1: Web Link
  int sy = py + 65;
  render_text_scaled(ren, "Web Link", px + 25, sy, 0.9f, accent_a);

  // Start server if not running
  if (!g_cast_server_running) start_cast_server();

  if (g_cast_url[0]) {
    // URL box
    SDL_Rect url_box = {px + 20, sy + 15, pw - 40, 36};
    fill_rounded_rect_hq(ren, url_box.x, url_box.y, url_box.w, url_box.h, 8, (SDL_Color){18, 18, 18, (Uint8)(255 * a)});
    draw_rounded_outline_hq(ren, url_box.x, url_box.y, url_box.w, url_box.h, 8, 1, (SDL_Color){60, 60, 60, (Uint8)(200 * a)});
    render_text_scaled(ren, g_cast_url, url_box.x + 12, url_box.y + 24, 0.9f, title_col);

    // Copy button
    SDL_Rect copy_btn = {px + pw - 90, sy + 58, 70, 28};
    bool hover_copy = (g_mouse_x >= copy_btn.x && g_mouse_x <= copy_btn.x + copy_btn.w &&
                       g_mouse_y >= copy_btn.y && g_mouse_y <= copy_btn.y + copy_btn.h);
    fill_rounded_rect_hq(ren, copy_btn.x, copy_btn.y, copy_btn.w, copy_btn.h, 6,
                         hover_copy ? (SDL_Color){g_theme.accent.r, g_theme.accent.g, g_theme.accent.b, (Uint8)(60 * a)}
                                    : (SDL_Color){40, 40, 40, (Uint8)(255 * a)});
    draw_rounded_outline_hq(ren, copy_btn.x, copy_btn.y, copy_btn.w, copy_btn.h, 6, 1, accent_a);
    render_text_scaled(ren, "Copy", copy_btn.x + 16, copy_btn.y + 20, 0.85f, accent_a);

    render_text_scaled(ren, "Open this link on any device or smart TV browser", px + 25, sy + 72, 0.75f, dim_col);
  } else {
    render_text_scaled(ren, "Starting server...", px + 25, sy + 30, 0.85f, dim_col);
  }

  // Section 2: Network Devices
  int dy = sy + 105;
  render_text_scaled(ren, "Network Devices", px + 25, dy, 0.9f, accent_a);

  // Scanning animation
  if (g_cast_scanning) {
    uint32_t elapsed = SDL_GetTicks() - g_cast_scan_start;
    int dots = (elapsed / 400) % 4;
    char scan_text[32] = "Scanning";
    for (int i = 0; i < dots; i++) strcat(scan_text, ".");
    render_text_scaled(ren, scan_text, px + 25, dy + 25, 0.8f, dim_col);

    if (elapsed > 4000) g_cast_scanning = false;
  }

  if (g_cast_client_connected) {
    SDL_Rect stop_btn = {px + pw - 130, py + 18, 100, 28};
    bool hover_stop = (g_mouse_x >= stop_btn.x && g_mouse_x <= stop_btn.x + stop_btn.w &&
                       g_mouse_y >= stop_btn.y && g_mouse_y <= stop_btn.y + stop_btn.h);
    fill_rounded_rect_hq(ren, stop_btn.x, stop_btn.y, stop_btn.w, stop_btn.h, 6,
                         hover_stop ? (SDL_Color){220, 60, 60, (Uint8)(255 * a)} : (SDL_Color){180, 40, 40, (Uint8)(255 * a)});
    render_text_scaled(ren, "Stop Cast", stop_btn.x + 12, stop_btn.y + 20, 0.85f, (SDL_Color){255, 255, 255, (Uint8)(255 * a)});
    
    if (hover_stop && (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT))) {
      g_cast_client_connected = false;
      send_player_command("VOLUME 100");
      show_toast("Casting Terminated.", 2000);
    }
  }

  if (g_cast_device_count == 0 && !g_cast_scanning) {
    render_text_scaled(ren, "No devices found on local network", px + 25, dy + 25, 0.8f, dim_col);
    // Rescan button
    SDL_Rect scan_btn = {px + pw - 90, dy + 8, 70, 28};
    bool hover_scan = (g_mouse_x >= scan_btn.x && g_mouse_x <= scan_btn.x + scan_btn.w &&
                       g_mouse_y >= scan_btn.y && g_mouse_y <= scan_btn.y + scan_btn.h);
    fill_rounded_rect_hq(ren, scan_btn.x, scan_btn.y, scan_btn.w, scan_btn.h, 6,
                         hover_scan ? (SDL_Color){40, 40, 40, (Uint8)(255 * a)} : (SDL_Color){30, 30, 30, (Uint8)(255 * a)});
    draw_rounded_outline_hq(ren, scan_btn.x, scan_btn.y, scan_btn.w, scan_btn.h, 6, 1, dim_col);
    render_text_scaled(ren, "Scan", scan_btn.x + 18, scan_btn.y + 20, 0.85f, dim_col);
  } else {
    for (int i = 0; i < g_cast_device_count && i < 4; i++) {
      SDL_Rect dev = {px + 20, dy + 20 + i * 44, pw - 40, 38};
      bool hover_dev = (g_mouse_x >= dev.x && g_mouse_x <= dev.x + dev.w &&
                        g_mouse_y >= dev.y && g_mouse_y <= dev.y + dev.h);
      fill_rounded_rect_hq(ren, dev.x, dev.y, dev.w, dev.h, 8,
                           hover_dev ? (SDL_Color){45, 45, 45, (Uint8)(255 * a)} : (SDL_Color){35, 35, 35, (Uint8)(255 * a)});
      render_text_scaled(ren, g_cast_devices[i].name, dev.x + 15, dev.y + 25, 0.9f, title_col);
      render_text_scaled(ren, g_cast_devices[i].ip, dev.x + dev.w - 110, dev.y + 25, 0.7f, dim_col);
    }
  }
}

void draw_aa_arc(SDL_Renderer *ren, int cx, int cy, int r, int start_ang,
                 int end_ang, int thickness, SDL_Color col) {
  SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
  for (int t = 0; t < thickness; t++) {
    int cur_r = r + t;
    for (int i = start_ang; i <= end_ang; i++) {
      float rad = i * 3.14159f / 180.0f;
      SDL_RenderDrawPoint(ren, cx + (int)(cosf(rad) * cur_r),
                          cy + (int)(sinf(rad) * cur_r));
    }
  }
  for (int i = start_ang; i <= end_ang; i++) {
    float rad = i * 3.14159f / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a / 2);
    SDL_RenderDrawPoint(ren, cx + (int)(c * (r + thickness)),
                        cy + (int)(s * (r + thickness)));
    SDL_RenderDrawPoint(ren, cx + (int)(c * (r - 1)), cy + (int)(s * (r - 1)));
  }
}

void fill_rounded_rect_hq(SDL_Renderer *ren, int x, int y, int w, int h, int r,
                          SDL_Color col) {
  SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
  SDL_Rect rects[3] = {{x + r, y, w - 2 * r, h},
                       {x, y + r, r, h - 2 * r},
                       {x + w - r, y + r, r, h - 2 * r}};
  SDL_RenderFillRects(ren, rects, 3);

  for (int i = 0; i < r; i++) {
    for (int j = 0; j < r; j++) {
      float dx = i + 0.5f;
      float dy = j + 0.5f;
      float dist = sqrtf(dx * dx + dy * dy);
      
      if (dist <= r + 0.5f) {
        float alpha_mult = 1.0f;
        if (dist > r - 0.5f) {
          alpha_mult = (r + 0.5f) - dist;
        }
        SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, (Uint8)(col.a * alpha_mult));
        SDL_RenderDrawPoint(ren, x + r - i - 1, y + r - j - 1); // TL
        SDL_RenderDrawPoint(ren, x + w - r + i, y + r - j - 1); // TR
        SDL_RenderDrawPoint(ren, x + r - i - 1, y + h - r + j); // BL
        SDL_RenderDrawPoint(ren, x + w - r + i, y + h - r + j); // BR
      }
    }
  }
}

void draw_rounded_mask_hq(SDL_Renderer *ren, int x, int y, int w, int h, int r,
                          SDL_Color bg_col) {
  for (int i = 0; i < r; i++) {
    for (int j = 0; j < r; j++) {
      float dx = i + 0.5f;
      float dy = j + 0.5f;
      float dist = sqrtf(dx * dx + dy * dy);
      
      if (dist > r - 0.5f) {
        float alpha_mult = dist - (r - 0.5f);
        if (alpha_mult > 1.0f) alpha_mult = 1.0f;
        
        SDL_SetRenderDrawColor(ren, bg_col.r, bg_col.g, bg_col.b, (Uint8)(255 * alpha_mult));
        SDL_RenderDrawPoint(ren, x + r - i - 1, y + r - j - 1); // TL
        SDL_RenderDrawPoint(ren, x + w - r + i, y + r - j - 1); // TR
        SDL_RenderDrawPoint(ren, x + r - i - 1, y + h - r + j); // BL
        SDL_RenderDrawPoint(ren, x + w - r + i, y + h - r + j); // BR
      }
    }
  }
}

void draw_rounded_outline_hq(SDL_Renderer *ren, int x, int y, int w, int h,
                             int r, int thickness, SDL_Color col) {
  SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
  SDL_Rect edges[4] = {
      {x + r, y, w - 2 * r, thickness},
      {x + r, y + h - thickness, w - 2 * r, thickness},
      {x, y + r, thickness, h - 2 * r},
      {x + w - thickness, y + r, thickness, h - 2 * r},
  };
  SDL_RenderFillRects(ren, edges, 4);

  float outer_r = r - 0.5f;
  float inner_r = r - thickness - 0.5f;
  
  for (int i = 0; i <= r; i++) {
    for (int j = 0; j <= r; j++) {
      float dx = i + 0.5f;
      float dy = j + 0.5f;
      float dist = sqrtf(dx * dx + dy * dy);
      
      if (dist > inner_r && dist <= outer_r + 1.0f) {
        float alpha_mult = 1.0f;
        if (dist > outer_r) {
          alpha_mult = outer_r + 1.0f - dist;
        } else if (dist < inner_r + 1.0f) {
          alpha_mult = dist - inner_r;
        }
        
        if (alpha_mult > 0.0f) {
          SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, (Uint8)(col.a * alpha_mult));
          SDL_RenderDrawPoint(ren, x + r - i - 1, y + r - j - 1);
          SDL_RenderDrawPoint(ren, x + w - r + i, y + r - j - 1);
          SDL_RenderDrawPoint(ren, x + r - i - 1, y + h - r + j);
          SDL_RenderDrawPoint(ren, x + w - r + i, y + h - r + j);
        }
      }
    }
  }
}

void draw_play_pause_button(SDL_Renderer *ren, int x, int y, int radius,
                            bool playing, SDL_Color col) {
  draw_rounded_outline_hq(ren, x - radius, y - radius, radius * 2, radius * 2,
                          radius, 3, col);
  SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
  
  int s = radius / 3;
  if (!playing) {
    for (int i = -s; i <= s; i++) {
      SDL_RenderDrawLine(ren, x - s/2, y + i, x + s - abs(i), y + i);
    }
  } else {
    int bw = s / 2;
    if (bw < 2) bw = 2;
    SDL_Rect bar1 = {x - s + bw/2, y - s, bw, s * 2};
    SDL_Rect bar2 = {x + bw/2, y - s, bw, s * 2};
    SDL_RenderFillRect(ren, &bar1);
    SDL_RenderFillRect(ren, &bar2);
  }
}

void draw_vertical_gradient(SDL_Renderer *ren, SDL_Rect rect, SDL_Color c1, SDL_Color c2) {
  static SDL_Texture *grad_tex = NULL;
  const int TEX_H = 256;
  if (!grad_tex) {
    grad_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 1, TEX_H);
    SDL_SetTextureBlendMode(grad_tex, SDL_BLENDMODE_BLEND);
  }

  void *pixels;
  int pitch;
  if (SDL_LockTexture(grad_tex, NULL, &pixels, &pitch) == 0) {
    uint32_t *p = (uint32_t *)pixels;
    for (int i = 0; i < TEX_H; i++) {
      float t = (float)i / (float)(TEX_H - 1);
      
      // Cubic easing for a more natural "premium" flow
      float et = t * t * (3.0f - 2.0f * t);
      
      // Add a tiny bit of pseudo-random noise to break 8-bit banding
      float noise = (((i * 13) % 100) - 50) / 255.0f * 0.5f; 
      
      uint8_t r = (uint8_t)fmax(0, fmin(255, (c1.r + (c2.r - c1.r) * et) + noise));
      uint8_t g = (uint8_t)fmax(0, fmin(255, (c1.g + (c2.g - c1.g) * et) + noise));
      uint8_t b = (uint8_t)fmax(0, fmin(255, (c1.b + (c2.b - c1.b) * et) + noise));
      uint8_t a = (uint8_t)fmax(0, fmin(255, (c1.a + (c2.a - c1.a) * et)));
      
      p[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }
    SDL_UnlockTexture(grad_tex);
  }
  
  SDL_RenderCopy(ren, grad_tex, NULL, &rect);
}

/* =========================================================================
 * UI Layer (Rendering)
 * ========================================================================= */

void init_orb_texture(SDL_Renderer *ren) {
  g_orb_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STATIC, 256, 256);
  SDL_SetTextureBlendMode(g_orb_tex, SDL_BLENDMODE_BLEND);
  uint32_t *px = malloc(256 * 256 * 4);
  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      float dx = x - 128.0f;
      float dy = y - 128.0f;
      float dist = sqrtf(dx * dx + dy * dy);
      float alpha = 0.0f;
      if (dist < 128.0f) {
        // Gaussian blur falloff - stronger spread
        alpha = expf(-(dist * dist) / 6000.0f);
        // Smoothly zero out at the very edge
        float edge_fade = (128.0f - dist) / 30.0f;
        if (edge_fade < 1.0f) alpha *= edge_fade;
      }
      uint8_t a = (uint8_t)(alpha * 255);
      px[y * 256 + x] = (a << 24) | 0xFFFFFF;
    }
  }
  SDL_UpdateTexture(g_orb_tex, NULL, px, 256 * 4);
  free(px);
}

Result init_font_system(SDL_Renderer *ren) {
  long size;
  unsigned char *buf;
  FILE *f = fopen(FONT_PATH, "rb");
  if (!f)
    return RESULT_ERROR_FILE_IO;
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  buf = malloc(size);
  fread(buf, 1, size, f);
  fclose(f);

  // Regular Font (20px)
  unsigned char *bmp = malloc(512 * 512);
  stbtt_BakeFontBitmap(buf, 0, 20.0, bmp, 512, 512, 32, 96, g_cdata);
  g_font_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                 SDL_TEXTUREACCESS_STATIC, 512, 512);
  uint32_t *px = malloc(512 * 512 * 4);
  for (int i = 0; i < 512 * 512; i++)
    px[i] = (bmp[i] << 24) | 0xFFFFFF;
  SDL_UpdateTexture(g_font_tex, NULL, px, 512 * 4);
  SDL_SetTextureBlendMode(g_font_tex, SDL_BLENDMODE_BLEND);
  free(bmp);
  free(px);

  // Header Font (64px) - Much Sharper
  unsigned char *h_bmp = malloc(1024 * 1024);
  stbtt_BakeFontBitmap(buf, 0, 64.0, h_bmp, 1024, 1024, 32, 96, g_hdata);
  g_hdr_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STATIC, 1024, 1024);
  uint32_t *h_px = malloc(1024 * 1024 * 4);
  for (int i = 0; i < 1024 * 1024; i++)
    h_px[i] = (h_bmp[i] << 24) | 0xFFFFFF;
  SDL_UpdateTexture(g_hdr_tex, NULL, h_px, 1024 * 4);
  SDL_SetTextureBlendMode(g_hdr_tex, SDL_BLENDMODE_BLEND);
  free(h_bmp);
  free(h_px);

  free(buf);
  return RESULT_SUCCESS;
}

void render_text_scaled(SDL_Renderer *ren, const char *text, float x, float y,
                        float scale, SDL_Color col) {
  if (!g_font_tex || !text)
    return;
  SDL_SetTextureColorMod(g_font_tex, col.r, col.g, col.b);
  SDL_SetTextureAlphaMod(g_font_tex, col.a);
  float curr_x = x;
  while (*text) {
    if (*text >= 32 && *text < 128) {
      stbtt_aligned_quad q;
      float next_x = curr_x;
      float next_y = y;
      stbtt_GetBakedQuad(g_cdata, 512, 512, *text - 32, &next_x, &next_y, &q,
                         1);

      float w = (q.x1 - q.x0) * scale;
      float h = (q.y1 - q.y0) * scale;
      float dx = (q.x0 - curr_x) * scale;
      float dy = (q.y0 - y) * scale;

      SDL_Rect src = {(int)(q.s0 * 512), (int)(q.t0 * 512),
                      (int)((q.s1 - q.s0) * 512), (int)((q.t1 - q.t0) * 512)};
      SDL_Rect dst = {(int)(curr_x + dx), (int)(y + dy), (int)w, (int)h};
      SDL_RenderCopy(ren, g_font_tex, &src, &dst);

      curr_x += (next_x - curr_x) * scale;
    }
    text++;
  }
}

void render_text_hdr_scaled(SDL_Renderer *ren, const char *text, float x, float y,
                            float scale, SDL_Color col) {
  if (!g_hdr_tex || !text)
    return;
  SDL_SetTextureColorMod(g_hdr_tex, col.r, col.g, col.b);
  SDL_SetTextureAlphaMod(g_hdr_tex, col.a);
  float curr_x = x;
  while (*text) {
    if (*text >= 32 && *text < 128) {
      stbtt_aligned_quad q;
      float next_x = curr_x;
      float next_y = y;
      stbtt_GetBakedQuad(g_hdata, 1024, 1024, *text - 32, &next_x, &next_y, &q, 1);

      float w = (q.x1 - q.x0) * scale;
      float h = (q.y1 - q.y0) * scale;
      float dx = (q.x0 - curr_x) * scale;
      float dy = (q.y0 - y) * scale;

      SDL_Rect src = {(int)(q.s0 * 1024), (int)(q.t0 * 1024),
                      (int)((q.s1 - q.s0) * 1024), (int)((q.t1 - q.t0) * 1024)};
      SDL_Rect dst = {(int)(curr_x + dx), (int)(y + dy), (int)w, (int)h};
      SDL_RenderCopy(ren, g_hdr_tex, &src, &dst);

      curr_x += (next_x - curr_x) * scale;
    }
    text++;
  }
}

void render_text(SDL_Renderer *ren, const char *text, float x, float y,
                 SDL_Color col) {
  render_text_scaled(ren, text, x, y, 1.0f, col);
}

void render_gradient_header(SDL_Renderer *ren, const char *text, int x, int y,
                             SDL_Color c1, SDL_Color c2, float alpha) {
  if (!g_hdr_tex || !text)
    return;
  SDL_SetTextureAlphaMod(g_hdr_tex, (Uint8)(255 * alpha));
  int len = strlen(text);
  float curr_x = (float)x;
  float curr_y = (float)y;
  for (int i = 0; i < len; i++) {
    float t = (float)i / (float)fmax(1, len - 1);
    SDL_Color col = {(uint8_t)(c1.r + (c2.r - c1.r) * t),
                     (uint8_t)(c1.g + (c2.g - c1.g) * t),
                     (uint8_t)(c1.b + (c2.b - c1.b) * t), 255};
    if (text[i] >= 32 && text[i] < 128) {
      stbtt_aligned_quad q;
      stbtt_GetBakedQuad(g_hdata, 1024, 1024, text[i] - 32, &curr_x, &curr_y, &q,
                         1);

      SDL_Rect src = {(int)(q.s0 * 1024), (int)(q.t0 * 1024),
                      (int)((q.s1 - q.s0) * 1024), (int)((q.t1 - q.t0) * 1024)};
      SDL_Rect dst = {(int)q.x0, (int)q.y0, (int)(q.x1 - q.x0),
                      (int)(q.y1 - q.y0)};

      SDL_SetTextureColorMod(g_hdr_tex, col.r, col.g, col.b);
      SDL_RenderCopy(ren, g_hdr_tex, &src, &dst);
    }
  }
}

void render_hero_header(SDL_Renderer *ren, const char *text, int cx, int cy, float progress, bool is_exit) {
    if (!text || !g_hdr_tex) return;
    int len = strlen(text);
    float total_w = get_text_width_hdr_scaled(text, 1.0f);
    float start_x = cx - total_w / 2.0f;
    float curr_x = start_x;
    float curr_y = (float)cy;

    for (int i = 0; i < len; i++) {
        // Stagger logic: each char has its own progress window
        float stagger = i * 0.05f;
        float char_p = (progress - stagger) / 0.6f;
        if (char_p < 0.0f) char_p = 0.0f;
        if (char_p > 1.0f) char_p = 1.0f;

        // Easing: Cubic Out for exit, Back Out for entry
        float ease_p;
        if (is_exit) {
            ease_p = char_p * char_p * char_p; // Cubic In for exit
        } else {
            // Back Out
            float c1 = 1.70158f;
            float c3 = c1 + 1.0f;
            ease_p = 1.0f + c3 * powf(char_p - 1.0f, 3.0f) + c1 * powf(char_p - 1.0f, 2.0f);
        }

        float y_off = is_exit ? -ease_p * 40.0f : (1.0f - ease_p) * 40.0f;
        float alpha = is_exit ? (1.0f - char_p) : char_p;

        SDL_Color c1 = {255, 255, 255, 255};
        SDL_Color c2 = g_theme.accent;
        float grad_t = (float)i / (float)fmax(1, len - 1);
        SDL_Color col = {(uint8_t)(c1.r + (c2.r - c1.r) * grad_t),
                         (uint8_t)(c1.g + (c2.g - c1.g) * grad_t),
                         (uint8_t)(c1.b + (c2.b - c1.b) * grad_t), (Uint8)(255 * alpha)};

        if (text[i] >= 32 && text[i] < 128) {
            stbtt_aligned_quad q;
            float next_x = curr_x;
            float next_y = curr_y;
            stbtt_GetBakedQuad(g_hdata, 1024, 1024, text[i] - 32, &next_x, &next_y, &q, 1);

            float base_w = q.x1 - q.x0;
            float base_h = q.y1 - q.y0;

            // Staggered Scaling
            float scale_ease = is_exit ? (1.0f - ease_p * 0.15f) : (0.85f + ease_p * 0.15f);
            float dw = base_w * scale_ease;
            float dh = base_h * scale_ease;

            SDL_Rect src = {(int)(q.s0 * 1024), (int)(q.t0 * 1024),
                            (int)((q.s1 - q.s0) * 1024), (int)((q.t1 - q.t0) * 1024)};
            SDL_Rect dst = {(int)(q.x0 + (base_w - dw) / 2.0f), 
                            (int)(q.y0 + (base_h - dh) / 2.0f + y_off), 
                            (int)dw, (int)dh};

            // Chromatic Aberration Effect during transition
            if (char_p > 0.05f && char_p < 0.95f) {
                float shift = (is_exit ? ease_p : (1.0f - ease_p)) * 8.0f;
                
                SDL_BlendMode old_blend;
                SDL_GetTextureBlendMode(g_hdr_tex, &old_blend);
                SDL_SetTextureBlendMode(g_hdr_tex, SDL_BLENDMODE_ADD);
                SDL_SetTextureAlphaMod(g_hdr_tex, (Uint8)(col.a * 0.8f));

                // Red
                SDL_SetTextureColorMod(g_hdr_tex, 255, 0, 0);
                SDL_Rect dstR = {dst.x - (int)shift, dst.y, dst.w, dst.h};
                SDL_RenderCopy(ren, g_hdr_tex, &src, &dstR);

                // Green
                SDL_SetTextureColorMod(g_hdr_tex, 0, 255, 0);
                SDL_RenderCopy(ren, g_hdr_tex, &src, &dst);

                // Blue
                SDL_SetTextureColorMod(g_hdr_tex, 0, 0, 255);
                SDL_Rect dstB = {dst.x + (int)shift, dst.y, dst.w, dst.h};
                SDL_RenderCopy(ren, g_hdr_tex, &src, &dstB);

                SDL_SetTextureBlendMode(g_hdr_tex, old_blend);
            } else {
                SDL_SetTextureColorMod(g_hdr_tex, col.r, col.g, col.b);
                SDL_SetTextureAlphaMod(g_hdr_tex, col.a);
                SDL_RenderCopy(ren, g_hdr_tex, &src, &dst);
            }

            // Light Sweep (Shimmer)
            float shimmer_p = fmodf(SDL_GetTicks() * 0.001f, 2.0f); // Sweep every 2s
            float char_x_norm = (curr_x - start_x) / total_w;
            float shimmer_dist = fabsf(char_x_norm - shimmer_p);
            if (shimmer_dist < 0.1f && !is_exit && progress > 0.9f && g_is_playing) {
                float shimmer_boost = (1.0f - shimmer_dist / 0.1f) * 150;
                SDL_SetTextureBlendMode(g_hdr_tex, SDL_BLENDMODE_ADD);
                SDL_SetTextureColorMod(g_hdr_tex, (Uint8)shimmer_boost, (Uint8)shimmer_boost, (Uint8)shimmer_boost);
                SDL_SetTextureAlphaMod(g_hdr_tex, (Uint8)shimmer_boost);
                SDL_RenderCopy(ren, g_hdr_tex, &src, &dst);
                SDL_SetTextureBlendMode(g_hdr_tex, SDL_BLENDMODE_BLEND);
            }

            curr_x = next_x;
        }
    }
}

float get_text_width_scaled(const char *text, float scale) {
  if (!text) return 0.0f;
  float curr_x = 0.0f;
  while (*text) {
    if (*text >= 32 && *text < 128) {
      stbtt_aligned_quad q;
      float next_x = curr_x;
      float next_y = 0;
      stbtt_GetBakedQuad(g_cdata, 512, 512, *text - 32, &next_x, &next_y, &q, 1);
      curr_x += (next_x - curr_x) * scale;
    }
    text++;
  }
  return curr_x;
}

float get_text_width_hdr_scaled(const char *text, float scale) {
  if (!text) return 0.0f;
  float curr_x = 0.0f;
  while (*text) {
    if (*text >= 32 && *text < 128) {
      stbtt_aligned_quad q;
      float next_x = curr_x;
      float next_y = 0;
      stbtt_GetBakedQuad(g_hdata, 1024, 1024, *text - 32, &next_x, &next_y, &q, 1);
      curr_x += (next_x - curr_x) * scale;
    }
    text++;
  }
  return curr_x;
}

void render_marquee_text(SDL_Renderer *ren, const char *text, float x, float y,
                         float avail_w, float scale, SDL_Color col, bool hdr) {
  if (!text || avail_w <= 0) return;
  float text_w = hdr ? get_text_width_hdr_scaled(text, scale) : get_text_width_scaled(text, scale);

  if (text_w <= avail_w) {
    if (hdr) render_text_hdr_scaled(ren, text, x, y, scale, col);
    else render_text_scaled(ren, text, x, y, scale, col);
    return;
  }

  SDL_Rect old_clip;
  SDL_bool has_old_clip = SDL_RenderIsClipEnabled(ren);
  if (has_old_clip) SDL_RenderGetClipRect(ren, &old_clip);

  SDL_Rect text_clip = {(int)x, (int)(y - 100), (int)avail_w, 200};
  SDL_RenderSetClipRect(ren, &text_clip);

  float loop_w = text_w + 100.0f;
  float offset = fmodf(SDL_GetTicks() * 0.04f, loop_w);

  if (hdr) {
    render_text_hdr_scaled(ren, text, x - offset, y, scale, col);
    render_text_hdr_scaled(ren, text, x - offset + loop_w, y, scale, col);
  } else {
    render_text_scaled(ren, text, x - offset, y, scale, col);
    render_text_scaled(ren, text, x - offset + loop_w, y, scale, col);
  }

  if (has_old_clip) SDL_RenderSetClipRect(ren, &old_clip);
  else SDL_RenderSetClipRect(ren, NULL);
}

void render_music_widget(SDL_Renderer *ren, int x, int y, int w, int h, int mx,
                         int my, GlobalWidgetInfo *info) {
  float max_t = 0.0f;
  if (h > 170) {
      max_t = (h - 160.0f) / (float)(g_window_height - TOP_BAR_HEIGHT - 160.0f);
      if (max_t > 1.0f) max_t = 1.0f;
      if (max_t < 0.0f) max_t = 0.0f;
  }

  int r = (int)(24.0f * (1.0f - max_t));
  int border_thickness = 4;

  // Widget Shadow/Base
  fill_rounded_rect_hq(ren, x, y, w, h, r, (SDL_Color){25, 25, 25, 255});

  if (!(*info->player_running) || !(*info->song_selected)) {
    // Elegant Placeholder
    int art_size = h - 50;
    SDL_Rect art_placeholder = {x + 25, y + 25, art_size, art_size};
    fill_rounded_rect_hq(ren, art_placeholder.x, art_placeholder.y, art_placeholder.w, art_placeholder.h, 15, (SDL_Color){35, 35, 35, 255});
    
    // Breathing Standby Visualizer
    if (g_orb_tex) {
        float breathe = (sinf(SDL_GetTicks() * 0.002f) + 1.0f) * 0.5f; // 0..1
        Uint8 alpha = (Uint8)(10 + breathe * 25);
        SDL_SetTextureBlendMode(g_orb_tex, SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(g_orb_tex, info->theme->accent.r, info->theme->accent.g, info->theme->accent.b);
        SDL_SetTextureAlphaMod(g_orb_tex, alpha);
        
        float orb_sz = art_size * (0.6f + breathe * 0.1f);
        SDL_Rect orb_r = {art_placeholder.x + (int)((art_size - orb_sz) / 2), 
                          art_placeholder.y + (int)((art_size - orb_sz) / 2), 
                          (int)orb_sz, (int)orb_sz};
        SDL_RenderCopy(ren, g_orb_tex, NULL, &orb_r);
        SDL_SetTextureBlendMode(g_orb_tex, SDL_BLENDMODE_BLEND);
    }

    draw_rounded_outline_hq(ren, art_placeholder.x, art_placeholder.y, art_placeholder.w, art_placeholder.h, 15, 1, (SDL_Color){50, 50, 50, 255});
    
    // Text labels
    render_text(ren, "Music Player", x + h + 10, y + h / 2 - 15, info->theme->text_main);
    render_text_scaled(ren, "Standby • Ready to play", x + h + 10, y + h / 2 + 25, 0.85f, info->theme->text_dim);
    
    draw_rounded_outline_hq(ren, x, y, w, h, r, 2, (SDL_Color){40, 40, 40, 255});
    return;
  }

  SDL_Rect card_rect = {x, y, w, h};
  SDL_RenderSetClipRect(ren, &card_rect);

  if (info->art_cache->blurred) {
    SDL_SetTextureAlphaMod(info->art_cache->blurred, 150);
    SDL_RenderCopy(ren, info->art_cache->blurred, NULL, &card_rect);
  }

  if (max_t > 0.0f) {
      SDL_Color grad_top = info->theme->target_accent;
      SDL_Color grad_bot = info->theme->bg;
      grad_top.a = (Uint8)(255 * max_t);
      grad_bot.a = (Uint8)(255 * max_t);
      draw_vertical_gradient(ren, card_rect, grad_top, grad_bot);

      if (g_orb_tex) {
          Uint32 now = SDL_GetTicks();
          float t1 = now * 0.0004f;
          float t2 = now * 0.0003f + 2.0f;
          float t3 = now * 0.0005f + 4.0f;

          SDL_Color c = info->theme->target_accent;
          float cx = x + w / 2.0f;
          float cy = y + h / 2.0f;
          float R_x = w * 0.35f; 
          float R_y = h * 0.35f;

          SDL_SetTextureBlendMode(g_orb_tex, SDL_BLENDMODE_ADD);
          
          float orb_w = w * 0.8f; 
          float orb_h = w * 0.8f; 
          Uint8 alpha = (Uint8)(50 * max_t);
          
          SDL_SetTextureColorMod(g_orb_tex, c.r, c.g, c.b);
          SDL_SetTextureAlphaMod(g_orb_tex, alpha);

          SDL_Rect o1 = { (int)(cx + cosf(t1) * R_x - orb_w/2), (int)(cy + sinf(t1 * 0.8f) * R_y - orb_h/2), (int)orb_w, (int)orb_h };
          SDL_RenderCopy(ren, g_orb_tex, NULL, &o1);
          
          float orb_w2 = w * 0.7f;
          SDL_Rect o2 = { (int)(cx + sinf(t2) * R_x * 0.8f - orb_w2/2), (int)(cy + cosf(t2 * 1.1f) * R_y * 0.9f - orb_w2/2), (int)orb_w2, (int)orb_w2 };
          SDL_RenderCopy(ren, g_orb_tex, NULL, &o2);

          float orb_w3 = w * 0.9f;
          SDL_Rect o3 = { (int)(cx + cosf(t3 * 0.9f) * R_x * 0.6f - orb_w3/2), (int)(cy + sinf(t3 * 1.2f) * R_y * 0.7f - orb_w3/2), (int)orb_w3, (int)orb_w3 };
          SDL_RenderCopy(ren, g_orb_tex, NULL, &o3);
          
          SDL_SetTextureBlendMode(g_orb_tex, SDL_BLENDMODE_BLEND);
      }
  }

  draw_rounded_mask_hq(ren, x, y, w, h, r, info->theme->bg);
  
  if (max_t < 1.0f) {
      SDL_Color border_col = info->theme->accent;
      border_col.a = (Uint8)(255 * (1.0f - max_t));
      draw_rounded_outline_hq(ren, x, y, w, h, r, border_thickness, border_col);
  }

  // Determine Layout based on Width (Stages)
  bool show_text = (w > 200);
  bool show_side_controls = (w > 600);

  float wid_art_size = h - 50.0f;
  float wid_art_x = x + 25.0f;
  float wid_art_y = y + 25.0f;

  float wid_text_x = x + h + 10.0f;
  float wid_text_y_top = y + h / 2.0f - 25.0f;
  float wid_text_y_bot = y + h / 2.0f + 15.0f;

  float max_art_size = h * 0.5f;
  float max_art_x = x + 80.0f;
  float max_art_y = y + (h - max_art_size) / 2.0f;

  float max_text_x = max_art_x + max_art_size + 60.0f;
  float max_text_y_top = max_art_y + 30.0f;
  float max_text_y_bot = max_text_y_top + 70.0f;

  float art_size = wid_art_size * (1.0f - max_t) + max_art_size * max_t;
  float art_x = wid_art_x * (1.0f - max_t) + max_art_x * max_t;
  float art_y = wid_art_y * (1.0f - max_t) + max_art_y * max_t;

  float text_x = wid_text_x * (1.0f - max_t) + max_text_x * max_t;
  float text_y_top = wid_text_y_top * (1.0f - max_t) + max_text_y_top * max_t;
  float text_y_bot = wid_text_y_bot * (1.0f - max_t) + max_text_y_bot * max_t;

  SDL_Rect art_r = {(int)art_x, (int)art_y, (int)art_size, (int)art_size};
  bool mouse_over_art = (mx >= art_r.x && mx <= art_r.x + art_r.w &&
                         my >= art_r.y && my <= art_r.y + art_r.h);

  if (info->art_cache->sharp) {
    SDL_RenderCopy(ren, info->art_cache->sharp, NULL, &art_r);

    // Play/Pause Overlay for Stage 2 & 3 in widget mode
    if (max_t == 0.0f && !show_side_controls && mouse_over_art) {
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 80); // Lighter dim
      SDL_RenderFillRect(ren, &art_r);

      SDL_Color btn_col = {255, 255, 255, 220};
      draw_play_pause_button(ren, art_r.x + art_r.w / 2, art_r.y + art_r.h / 2,
                             30, *info->is_playing, btn_col);
    }
  }

  if (show_text || max_t > 0.0f) {
    float responsive_scale = (w - max_text_x - 50.0f) / 500.0f;
    if (responsive_scale > 1.0f) responsive_scale = 1.0f;
    if (responsive_scale < 0.4f) responsive_scale = 0.4f;

    float title_scale = (24.0f / 64.0f) * (1.0f - max_t) + responsive_scale * max_t;
    float artist_scale = (20.0f / 64.0f) * (1.0f - max_t) + (responsive_scale * 0.5f) * max_t;

    uint8_t alpha = 255;
    if (w < 350 && max_t == 0.0f)
      alpha = (uint8_t)((w - 200) * 255 / 150);

    SDL_Color main_col = info->theme->text_main;
    main_col.a = alpha;
    SDL_Color dim_col = info->theme->text_dim;
    dim_col.a = alpha;

    float avail_w = (x + w) - text_x - 25.0f;
    if (show_side_controls && max_t < 1.0f) avail_w -= 80.0f; 

    render_marquee_text(ren, info->current_track->title, text_x, text_y_top, avail_w, title_scale, main_col, true);
    render_marquee_text(ren, info->current_track->artist, text_x, text_y_bot, avail_w, artist_scale, dim_col, true);
  }

  if (show_side_controls && max_t < 1.0f) {
    SDL_Color btn_col = info->theme->accent;
    btn_col.a = (Uint8)(255 * (1.0f - max_t));
    draw_play_pause_button(ren, x + w - 70, y + h / 2, 30, *info->is_playing, btn_col);
  }

  if (max_t > 0.5f) {
      float fade = (max_t - 0.5f) * 2.0f; // 0.0 to 1.0
      
      float seek_w = w * 0.35f;
      if (max_text_x + seek_w > w - 50.0f) {
          seek_w = w - max_text_x - 50.0f;
      }
      if (seek_w < 100) seek_w = 100;
      
      float seek_x = max_text_x;
      float seek_y = max_text_y_bot + 90.0f;
      
      SDL_SetRenderDrawColor(ren, 255, 255, 255, (Uint8)(50 * fade));
      SDL_Rect seek_bg = {(int)seek_x, (int)seek_y, (int)seek_w, 4};
      SDL_RenderFillRect(ren, &seek_bg);
      
      float progress = 0.0f;
      if (info->current_track->duration > 0.01f) {
          progress = info->current_track->position / info->current_track->duration;
          if (progress > 1.0f) progress = 1.0f;
          if (progress < 0.0f) progress = 0.0f;
      }

      SDL_SetRenderDrawColor(ren, info->theme->target_accent.r, info->theme->target_accent.g, info->theme->target_accent.b, (Uint8)(255 * fade));
      SDL_Rect seek_fg = {(int)seek_x, (int)seek_y, (int)(seek_w * progress), 4};
      SDL_RenderFillRect(ren, &seek_fg);

      // Time Labels
      char time_buf[32];
      int cur_min = (int)info->current_track->position / 60;
      int cur_sec = (int)info->current_track->position % 60;
      snprintf(time_buf, sizeof(time_buf), "%d:%02d", cur_min, cur_sec);
      render_text_scaled(ren, time_buf, seek_x, seek_y + 25, 0.8f, (SDL_Color){200, 200, 200, (Uint8)(255 * fade)});

      int dur_min = (int)info->current_track->duration / 60;
      int dur_sec = (int)info->current_track->duration % 60;
      snprintf(time_buf, sizeof(time_buf), "%d:%02d", dur_min, dur_sec);
      float dur_text_w = get_text_width_scaled(time_buf, 0.8f);
      render_text_scaled(ren, time_buf, seek_x + seek_w - dur_text_w, seek_y + 25, 0.8f, (SDL_Color){200, 200, 200, (Uint8)(255 * fade)});

      float controls_x = seek_x + seek_w / 2.0f;
      float controls_y = seek_y + 80.0f;
      SDL_Color btn_col = {255, 255, 255, (Uint8)(255 * fade)};
      draw_play_pause_button(ren, (int)controls_x, (int)controls_y, 40, *info->is_playing, btn_col);
  }

  // Drag handle (Top center bar, visible only when mouse is near)
  if (max_t < 1.0f) {
      SDL_Rect drag_bar = {x + w / 2 - 25, y + 8, 50, 4};
      bool mouse_near_drag =
          (mx > x + w / 2 - 60 && mx < x + w / 2 + 60 && my > y && my < y + 40);

      if (mouse_near_drag || g_music_widget.is_dragging) {
        fill_rounded_rect_hq(ren, drag_bar.x, drag_bar.y, drag_bar.w, drag_bar.h, 2,
                             (SDL_Color){150, 150, 150, (Uint8)(200 * (1.0f - max_t))});
      }
  }

  // Resize handle (right edge, thinner and inside)
  if (max_t < 1.0f && (g_music_widget_hover || g_music_widget.is_resizing)) {
    SDL_Color handle_col = g_theme.accent;
    handle_col.a = (Uint8)(200 * (1.0f - max_t));
    fill_rounded_rect_hq(ren, x + w - 10, y + (h - 30) / 2, 4, 30, 2, handle_col);
  }

  // Down chevron to minimize
  if (max_t > 0.1f) {
      int cx = x + 50;
      int cy = y + 50;
      SDL_SetRenderDrawColor(ren, 255, 255, 255, (Uint8)(255 * max_t));
      SDL_RenderDrawLine(ren, cx - 10, cy - 5, cx, cy + 5);
      SDL_RenderDrawLine(ren, cx, cy + 5, cx + 10, cy - 5);
      SDL_RenderDrawLine(ren, cx - 10, cy - 4, cx, cy + 6);
      SDL_RenderDrawLine(ren, cx, cy + 6, cx + 10, cy - 4);
      SDL_RenderDrawLine(ren, cx - 10, cy - 6, cx, cy + 4);
      SDL_RenderDrawLine(ren, cx, cy + 4, cx + 10, cy - 6);

      // Cast Icon (Top Right)
      int tx = x + w - 70;
      int ty = y + 35;
      SDL_Color btn_col = {255, 255, 255, (Uint8)(255 * max_t)};
      
      // Outer Screen
      SDL_Rect screen = {tx, ty, 40, 30};
      draw_rounded_outline_hq(ren, screen.x, screen.y, screen.w, screen.h, 4, 2, btn_col);
      // Small filled dot in bottom left of icon
      SDL_Rect dot = {tx + 6, ty + 20, 6, 6};
      fill_rounded_rect_hq(ren, dot.x, dot.y, dot.w, dot.h, 3, btn_col);
      // Signal arcs
      draw_aa_arc(ren, tx + 6, ty + 26, 12, 270, 360, 2, btn_col);
      draw_aa_arc(ren, tx + 6, ty + 26, 18, 270, 360, 2, btn_col);
  }

  SDL_RenderSetClipRect(ren, NULL);
}

void render_now_playing_pill(SDL_Renderer *ren, int mx, int my) {
    if (!g_song_selected) return;

    int pill_h = 40;
    int pill_w = 260;
    int pill_x = g_window_width - pill_w - 30;
    int pill_y = 10;

    // Use ClipRect to ensure ABSOLUTELY NO bleed outside the pill boundaries
    SDL_Rect clip_r = {pill_x, pill_y, pill_w, pill_h};
    SDL_Rect old_clip;
    SDL_bool has_clip = SDL_RenderIsClipEnabled(ren);
    if (has_clip) SDL_RenderGetClipRect(ren, &old_clip);
    SDL_RenderSetClipRect(ren, &clip_r);

    // Check hover (Always interactive now)
    g_pill_hover = (mx >= pill_x && mx <= pill_x + pill_w && my >= pill_y && my <= pill_y + pill_h);

    // Pill Background (Match search bar: 40,40,40)
    SDL_Color bg_col = {40, 40, 40, 255};
    if (g_pill_hover) {
        bg_col.r += 15; bg_col.g += 15; bg_col.b += 15;
    }
    
    fill_rounded_rect_hq(ren, pill_x, pill_y, pill_w, pill_h, pill_h / 2, bg_col);
    
    // Spinning CD - Rendered to offscreen texture, then scanline-masked to a perfect circle.
    // This eliminates ALL square bleed because the texture pixels themselves are transparent.
    int art_size = 36;
    int art_x = pill_x + 20 - (art_size / 2);
    int art_y = pill_y + 20 - (art_size / 2);
    SDL_Rect art_rect = {art_x, art_y, art_size, art_size};

    if (g_is_playing) {
        g_pill_rotation += 1.8f;
        if (g_pill_rotation >= 360.0f) g_pill_rotation -= 360.0f;
    }

    if (g_art_cache.sharp) {
        // Lazy-init the offscreen compositing texture
        if (!g_cd_tex) {
            g_cd_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, art_size, art_size);
            SDL_SetTextureBlendMode(g_cd_tex, SDL_BLENDMODE_BLEND);
        }

        // --- STEP 1: Render rotated art to offscreen texture ---
        SDL_SetRenderTarget(ren, g_cd_tex);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
        SDL_RenderClear(ren);

        SDL_Rect cd_dst = {0, 0, art_size, art_size};
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_RenderCopyEx(ren, g_art_cache.sharp, NULL, &cd_dst,
                         (double)g_pill_rotation, NULL, SDL_FLIP_NONE);

        // --- STEP 2: Scanline circular mask (BLENDMODE_NONE = direct pixel replace) ---
        // Erase everything outside the inscribed circle with fully transparent pixels.
        // The inscribed circle of a square with side art_size has radius art_size/2.
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 0); // Fully transparent
        float cd_r = art_size / 2.0f;
        for (int row = 0; row < art_size; row++) {
            float dy = (row + 0.5f) - cd_r;
            float r_sq = cd_r * cd_r - dy * dy;
            if (r_sq <= 0.0f) {
                // Entire row is outside circle — clear it
                SDL_Rect clear_row = {0, row, art_size, 1};
                SDL_RenderFillRect(ren, &clear_row);
                continue;
            }
            float half_w = sqrtf(r_sq);
            int x_left = (int)(cd_r - half_w);
            int x_right = (int)ceilf(cd_r + half_w);
            if (x_left < 0) x_left = 0;
            if (x_right > art_size) x_right = art_size;

            // Clear left strip
            if (x_left > 0) {
                SDL_Rect left = {0, row, x_left, 1};
                SDL_RenderFillRect(ren, &left);
            }
            // Clear right strip
            if (x_right < art_size) {
                SDL_Rect right = {x_right, row, art_size - x_right, 1};
                SDL_RenderFillRect(ren, &right);
            }
        }

        // --- STEP 3: Switch back to screen and blit the circular texture ---
        SDL_SetRenderTarget(ren, NULL);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_RenderSetClipRect(ren, &clip_r); // Restore clip (target switch resets it)

        SDL_RenderCopy(ren, g_cd_tex, NULL, &art_rect);

        // --- CD Overlays (drawn on screen, on top of the circular art) ---
        // Center hole
        int hole_r = 3;
        fill_rounded_rect_hq(ren, art_x + art_size/2 - hole_r, art_y + art_size/2 - hole_r,
                             hole_r * 2, hole_r * 2, hole_r, bg_col);
        // Inner ring
        SDL_Color ring_col = {255, 255, 255, 25};
        draw_rounded_outline_hq(ren, art_x + art_size/2 - 5, art_y + art_size/2 - 5,
                                10, 10, 5, 1, ring_col);
    } else {
        SDL_Color ph_col = {60, 60, 60, 255};
        fill_rounded_rect_hq(ren, art_x, art_y, art_size, art_size, art_size / 2, ph_col);
        fill_rounded_rect_hq(ren, art_x + art_size/2 - 3, art_y + art_size/2 - 3, 6, 6, 3, bg_col);
    }

    // DRAW BORDER AFTER ART - Ensures the border masks any tiny bleed
    SDL_Color border_col = g_theme.accent;
    if (!g_is_playing) border_col = (SDL_Color){80, 80, 80, 255};
    draw_rounded_outline_hq(ren, pill_x, pill_y, pill_w, pill_h, pill_h / 2, 2, border_col);

    // Song Title Marquee - Slightly shifted to give the larger CD breathing room
    float text_x = pill_x + pill_h + 8;
    float text_y = pill_y + 27;
    float avail_w = (pill_x + pill_w) - text_x - 15;
    render_marquee_text(ren, g_current_track.title, text_x, text_y, avail_w, 0.95f, g_theme.text_main, false);

    // Reset ClipRect
    if (has_clip) SDL_RenderSetClipRect(ren, &old_clip);
    else SDL_RenderSetClipRect(ren, NULL);

    if (g_cast_client_connected) {
        // Pulse "Casting" label
        float pulse = (sinf(SDL_GetTicks() * 0.005f) + 1.0f) * 0.5f;
        SDL_Color cast_col = {g_theme.accent.r, g_theme.accent.g, g_theme.accent.b, (Uint8)(150 + pulse * 105)};
        render_text_scaled(ren, "• CASTING", pill_x + pill_w - 65, pill_y + 26, 0.7f, cast_col);
    }
}

#define GRID_FADE_RADIUS 400.0f
#define PLUS_ARM_LEN 7
#define PLUS_THICKNESS 2

static void calculate_grid_pos(int col, int row, float *x, float *y) {
  *x = (float)(GRID_MARGIN + col * GRID_CELL_W);
  *y = (float)(TOP_BAR_HEIGHT + HEADER_HEIGHT + row * GRID_CELL_H);
}

static void get_nearest_grid_slot(float x, float y, int *col, int *row) {
  *col = (int)roundf((x - GRID_MARGIN) / (float)GRID_CELL_W);
  *row = (int)roundf((y - (TOP_BAR_HEIGHT + HEADER_HEIGHT)) / (float)GRID_CELL_H);
  if (*col < 0) *col = 0;
  if (*row < 0) *row = 0;
}

static void render_grid_drop_guides(SDL_Renderer *ren, float widget_cx,
                                    float widget_cy) {
  int cols = (g_window_width - 2 * GRID_MARGIN) / GRID_CELL_W + 1;
  int rows = (g_window_height - (TOP_BAR_HEIGHT + HEADER_HEIGHT)) / GRID_CELL_H + 1;

  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      float gx, gy;
      calculate_grid_pos(col, row, &gx, &gy);

      /* Distance from widget center to this grid point */
      float dx = (float)gx - widget_cx;
      float dy = (float)gy - widget_cy;
      float dist = sqrtf(dx * dx + dy * dy);

      /* Fade: full alpha at distance 0, zero alpha at GRID_FADE_RADIUS */
      float t = dist / GRID_FADE_RADIUS;
      if (t >= 1.0f)
        continue; /* Too far away — skip entirely */

      /* Ease-out curve for smoother falloff */
      float alpha_f = (1.0f - t) * (1.0f - t);
      uint8_t alpha = (uint8_t)(alpha_f * 120.0f); /* Max 120/255 so it stays subtle */
      if (alpha < 5)
        continue;

      SDL_Color pc = g_theme.accent;

      /* Horizontal bar of the + */
      SDL_SetRenderDrawColor(ren, pc.r, pc.g, pc.b, alpha);
      SDL_Rect h_bar = {gx - PLUS_ARM_LEN, gy - PLUS_THICKNESS / 2,
                        PLUS_ARM_LEN * 2 + 1, PLUS_THICKNESS};
      SDL_RenderFillRect(ren, &h_bar);

      /* Vertical bar of the + */
      SDL_Rect v_bar = {gx - PLUS_THICKNESS / 2, gy - PLUS_ARM_LEN,
                        PLUS_THICKNESS, PLUS_ARM_LEN * 2 + 1};
      SDL_RenderFillRect(ren, &v_bar);

      /* Soft glow halo around the + (even fainter) */
      uint8_t glow_alpha = (uint8_t)(alpha_f * 30.0f);
      if (glow_alpha > 2) {
        SDL_SetRenderDrawColor(ren, pc.r, pc.g, pc.b, glow_alpha);
        int glow_r = PLUS_ARM_LEN + 4;
        for (int a = 0; a < 360; a += 6) {
          float rad = a * 3.14159f / 180.0f;
          SDL_RenderDrawPoint(ren, gx + (int)(cosf(rad) * glow_r),
                              gy + (int)(sinf(rad) * glow_r));
        }
      }
    }
  }
}

static void validate_widget_positions(void) {
  if (g_music_widget.is_maximized) {
    g_music_widget.target_x = 0;
    g_music_widget.target_y = 0;
    g_music_widget.target_w = g_window_width;
    g_music_widget.target_h = g_window_height;
    return;
  }

  int max_cols = (g_window_width - 2 * GRID_MARGIN) / GRID_CELL_W;
  if (max_cols < 1)
    max_cols = 1;

  // Rule 1: Always fill as far top left as possible if out of bounds
  // Rule 2: User priority (grid_col/grid_row) is kept if within bounds
  if (g_music_widget.grid_col >= max_cols) {
    // If it's forced to move, we wrap it to the next row
    g_music_widget.grid_row += g_music_widget.grid_col / max_cols;
    g_music_widget.grid_col %= max_cols;
  }

  float tx, ty;
  calculate_grid_pos(g_music_widget.grid_col, g_music_widget.grid_row, &tx, &ty);
  g_music_widget.target_x = tx;
  g_music_widget.target_y = ty;
}

#define RENDER_MUSIC_WIDGET_MACRO() \
    do { \
      SDL_SetRenderTarget(ren, g_widget_tex); \
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 0); \
      SDL_RenderClear(ren); \
      GlobalWidgetInfo info = {&g_theme, &g_current_track, &g_art_cache, \
                               &g_player_running, &g_song_selected, \
                               &g_is_playing, send_player_command}; \
      int off = 100; \
      int lmx = (g_mouse_x - (int)g_music_widget.x) + off; \
      int lmy = (g_mouse_y - (int)g_music_widget.y) + off; \
      if (g_music_widget.render) { \
        g_music_widget.render(ren, off, off, (int)g_music_widget.w, \
                              (int)g_music_widget.h, lmx, lmy, &info); \
      } \
      SDL_SetRenderTarget(ren, NULL); \
      SDL_Rect src = {off - 50, off - 50, (int)g_music_widget.w + 100, \
                      (int)g_music_widget.h + 100}; \
      SDL_Rect dst = {(int)g_music_widget.x - 50, (int)g_music_widget.y - 50, \
                      src.w, src.h}; \
      SDL_Point center = {50 + (int)g_music_widget.w / 2, \
                          50 + (int)g_music_widget.h / 2}; \
      SDL_RenderCopyEx(ren, g_widget_tex, &src, &dst, \
                       (double)g_music_widget.rotation, &center, SDL_FLIP_NONE); \
    } while (0)

// Separate render macro for the Pill Fullscreen overlay — uses g_pill_fs position
#define RENDER_PILL_FS_MACRO() \
    do { \
      SDL_SetRenderTarget(ren, g_widget_tex); \
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 0); \
      SDL_RenderClear(ren); \
      GlobalWidgetInfo info = {&g_theme, &g_current_track, &g_art_cache, \
                               &g_player_running, &g_song_selected, \
                               &g_is_playing, send_player_command}; \
      int off = 100; \
      int lmx = (g_mouse_x - (int)g_pill_fs.x) + off; \
      int lmy = (g_mouse_y - (int)g_pill_fs.y) + off; \
      render_music_widget(ren, off, off, (int)g_pill_fs.w, \
                          (int)g_pill_fs.h, lmx, lmy, &info); \
      SDL_SetRenderTarget(ren, NULL); \
      /* Fade out as the overlay shrinks back to pill size */ \
      float _fade = 1.0f; \
      if (!g_pill_fs.is_maximized) { \
          _fade = (g_pill_fs.h - 40.0f) / 260.0f; \
          if (_fade > 1.0f) _fade = 1.0f; \
          if (_fade < 0.0f) _fade = 0.0f; \
      } \
      SDL_SetTextureAlphaMod(g_widget_tex, (Uint8)(255 * _fade)); \
      SDL_Rect src = {off - 50, off - 50, (int)g_pill_fs.w + 100, \
                      (int)g_pill_fs.h + 100}; \
      SDL_Rect dst = {(int)g_pill_fs.x - 50, (int)g_pill_fs.y - 50, \
                      src.w, src.h}; \
      SDL_Point center = {50 + (int)g_pill_fs.w / 2, \
                          50 + (int)g_pill_fs.h / 2}; \
      SDL_RenderCopyEx(ren, g_widget_tex, &src, &dst, \
                       0.0, &center, SDL_FLIP_NONE); \
      SDL_SetTextureAlphaMod(g_widget_tex, 255); \
    } while (0)

int main(void) {
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *win =
      SDL_CreateWindow("Harmony Hub", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, g_window_width, g_window_height,
                       SDL_WINDOW_RESIZABLE);
  SDL_SetWindowMinimumSize(win, 600, 400);
  SDL_Renderer *ren = SDL_CreateRenderer(
      win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  g_widget_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                   SDL_TEXTUREACCESS_TARGET, 3000, 2000);
  SDL_SetTextureBlendMode(g_widget_tex, SDL_BLENDMODE_BLEND);
  init_font_system(ren);
  init_orb_texture(ren);
  load_hub_state();

  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  void *z_ctx = zmq_ctx_new();
  void *z_sock = zmq_socket(z_ctx, ZMQ_REP);
  zmq_bind(z_sock, "ipc:///tmp/hub_socket.ipc");
  g_z_cmd_sock = zmq_socket(z_ctx, ZMQ_PUB);
  zmq_bind(g_z_cmd_sock, "ipc:///tmp/hub_cmd.ipc");

  bool running = true;
  SDL_Event e;
  Uint32 last_check = 0;

  while (running) {
      // 1. Process Status Updates from Player
      char msg[2048];
      int n = zmq_recv(z_sock, msg, sizeof(msg) - 1, ZMQ_DONTWAIT);
      if (n > 0) {
        msg[n] = '\0';
        printf("[Hub] Syncing Status: %s\n", msg);
        
        // Robust status parser (Skip keys and find values correctly)
        char *t = strstr(msg, "\"title\"");
        if (t) {
          t = strchr(t, ':'); if (t) t = strchr(t, '\"');
          if (t) {
            t++; char *end = strchr(t, '\"');
            if (end) { int len = end - t; strncpy(g_current_track.title, t, len); g_current_track.title[len] = 0; }
          }
        }
        char *a = strstr(msg, "\"artist\"");
        if (a) {
          a = strchr(a, ':'); if (a) a = strchr(a, '\"');
          if (a) {
            a++; char *end = strchr(a, '\"');
            if (end) { int len = end - a; strncpy(g_current_track.artist, a, len); g_current_track.artist[len] = 0; }
          }
        }
        char *p = strstr(msg, "\"filepath\"");
        if (p) {
          p = strchr(p, ':'); if (p) p = strchr(p, '\"');
          if (p) {
            p++; char *end = strchr(p, '\"');
            if (end) { int len = end - p; strncpy(g_current_track.filepath, p, len); g_current_track.filepath[len] = 0; }
          }
        }
        char *art = strstr(msg, "\"art_path\"");
        if (art) {
          art = strchr(art, ':'); if (art) art = strchr(art, '\"');
          if (art) {
            art++; char *end = strchr(art, '\"');
            if (end) { int len = end - art; strncpy(g_current_track.art_path, art, len); g_current_track.art_path[len] = 0; }
          }
        }
        char *pos = strstr(msg, "\"position\":");
        if (pos) { pos = strchr(pos, ':'); if (pos) g_current_track.position = (float)atof(pos + 1); }
        char *dur = strstr(msg, "\"duration\":");
        if (dur) { dur = strchr(dur, ':'); if (dur) g_current_track.duration = (float)atof(dur + 1); }
        char *st = strstr(msg, "\"state\":");
        if (st) {
          st = strchr(st, ':');
          if (st) g_is_playing = (strstr(st, "playing") != NULL);
        }

        g_song_selected = true;
        zmq_send(z_sock, "ACK", 3, 0);
      }
    poll_net_discovery();
    if (g_cast_popup_open && g_cast_scanning) {
      static uint32_t last_scan_req = 0;
      if (SDL_GetTicks() - last_scan_req > 2000) {
        send_discovery_queries();
        last_scan_req = SDL_GetTicks();
      }
    }
    Uint32 now = SDL_GetTicks();
    if (now - last_check > 500) {
      update_player_status();
      if (!g_player_running) {
        g_song_selected = false;
        strcpy(g_current_track.title, "No active song");
        strcpy(g_current_track.artist, "Unknown Artist");
        g_current_track.art_path[0] = 0;
      }
      last_check = now;
    }

    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        show_modal("Exit Harmony Hub?", 
                   "This will stop the hub and the background music process. Any unsaved playback state will be lost.", 
                   "Quit & Stop", "Cancel", true, quit_action, cancel_quit);
      }
      
      if (g_modal.active) {
        if (e.type == SDL_MOUSEBUTTONDOWN) {
          int mw = 500, mh = 260;
          int mx = (g_window_width - mw) / 2;
          int my = (g_window_height - mh) / 2;
          
          // Cancel Button (mw - 310 to mw - 170)
          if (e.button.x > mx + mw - 310 && e.button.x < mx + mw - 170 &&
              e.button.y > my + mh - 70 && e.button.y < my + mh - 20) {
            if (g_modal.on_cancel) g_modal.on_cancel();
            g_modal.active = false;
          }
          // Confirm Button (mw - 150 to mw - 10)
          if (e.button.x > mx + mw - 150 && e.button.x < mx + mw - 10 &&
              e.button.y > my + mh - 70 && e.button.y < my + mh - 20) {
            if (g_modal.on_confirm) g_modal.on_confirm();
            g_modal.active = false;
          }
        }
        if (e.type == SDL_KEYDOWN) {
          if (e.key.keysym.sym == SDLK_ESCAPE) g_modal.active = false;
          if (e.key.keysym.sym == SDLK_RETURN && !g_modal.danger) {
             if (g_modal.on_confirm) g_modal.on_confirm();
             g_modal.active = false;
          }
        }
        continue; // Consume all events when modal is active
      }

      if (e.type == SDL_WINDOWEVENT) {
        if (e.window.event == SDL_WINDOWEVENT_RESIZED ||
            e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
          g_window_width = e.window.data1;
          g_window_height = e.window.data2;
          validate_widget_positions();
        }
      }
      if (e.type == SDL_TEXTINPUT) {
        strncat(g_search_query, e.text.text,
                MAX_SONG_TITLE - strlen(g_search_query) - 1);
        perform_search(g_search_query);
      }
      if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_BACKSPACE && strlen(g_search_query) > 0) {
          g_search_query[strlen(g_search_query) - 1] = 0;
          perform_search(g_search_query);
        }
        if (e.key.keysym.sym == SDLK_ESCAPE)
          g_sidebar_open = false;
        if (e.key.keysym.sym == SDLK_RETURN && strlen(g_search_query) > 0) {
           perform_search(g_search_query);
        }
      }
      if (e.type == SDL_MOUSEMOTION) {
        g_mouse_x = e.motion.x;
        g_mouse_y = e.motion.y;

        // Hover detection for Music Widget
        float wx = g_music_widget.x, wy = g_music_widget.y;
        float ww = g_music_widget.w, wh = g_music_widget.h;
        
        float cx = wx + ww / 2.0f;
        float cy = wy + wh / 2.0f;
        float ux = (float)g_mouse_x;
        float uy = (float)g_mouse_y;

        if (g_music_widget.rotation != 0.0f) {
            float rad = -g_music_widget.rotation * (M_PI / 180.0f);
            float s = sinf(rad);
            float c = cosf(rad);
            float tx = ux - cx;
            float ty = uy - cy;
            ux = tx * c - ty * s + cx;
            uy = tx * s + ty * c + cy;
        }

        g_music_widget_hover = (ux > wx && ux < wx + ww &&
                                uy > wy && uy < wy + wh);

        if (g_music_widget.is_resizing) {
          float new_w = g_mouse_x - g_music_widget.x;
          if (new_w < 160)
            new_w = 160;
          if (new_w > 860)
            new_w = 860;
          g_music_widget.w = new_w;
          g_music_widget.target_w = new_w;
        }

        if (g_music_widget.is_dragging) {
          float dx = (float)g_mouse_x - g_music_widget.drag_off_x - g_music_widget.x;
          float dy = (float)g_mouse_y - g_music_widget.drag_off_y - g_music_widget.y;
          if (dx*dx + dy*dy > 25) g_music_widget.was_dragged = true;

          g_music_widget.x = (float)g_mouse_x - g_music_widget.drag_off_x;
          g_music_widget.y = (float)g_mouse_y - g_music_widget.drag_off_y;
          g_music_widget.target_x = g_music_widget.x;
          g_music_widget.target_y = g_music_widget.y;
        }
      }
      if (e.type == SDL_MOUSEBUTTONDOWN) {
        // Cast popup interaction
        if (g_cast_popup_open) {
          int pw = 420, ph = 340;
          int px = (g_window_width - pw) / 2;
          int py = (g_window_height - ph) / 2;

          // Close X button
          int cx = px + pw - 35, cy = py + 25;
          if (e.button.x >= cx && e.button.x <= cx + 14 &&
              e.button.y >= cy && e.button.y <= cy + 14) {
            g_cast_popup_open = false;
            continue;
          }

          // Copy button
          int sy = py + 65;
          SDL_Rect copy_btn = {px + pw - 90, sy + 58, 70, 28};
          if (g_cast_url[0] && e.button.x >= copy_btn.x && e.button.x <= copy_btn.x + copy_btn.w &&
              e.button.y >= copy_btn.y && e.button.y <= copy_btn.y + copy_btn.h) {
            SDL_SetClipboardText(g_cast_url);
            show_toast("Link copied to clipboard!", 2000);
            continue;
          }

          // Scan button
          int dy = sy + 105;
          SDL_Rect scan_btn = {px + pw - 90, dy + 8, 70, 28};
          if (e.button.x >= scan_btn.x && e.button.x <= scan_btn.x + scan_btn.w &&
              e.button.y >= scan_btn.y && e.button.y <= scan_btn.y + scan_btn.h) {
            g_cast_scanning = true;
            g_cast_scan_start = SDL_GetTicks();
            send_discovery_queries();
            continue;
          }

          // Click outside popup closes it
          if (e.button.x < px || e.button.x > px + pw ||
              e.button.y < py || e.button.y > py + ph) {
            g_cast_popup_open = false;
          }
          continue;
        }

        // 1a. Priority: Pill Fullscreen Overlay (completely separate from grid widget)
        if (g_pill_fs.active && g_pill_fs.is_maximized) {
          float wx = g_pill_fs.x, wy = g_pill_fs.y;
          float ww = g_pill_fs.w, wh = g_pill_fs.h;

          // Check Chevron (Top Left) to minimize back to pill
          int cx = (int)wx + 50;
          int cy = (int)wy + 50;
          int dist_sq = (e.button.x - cx) * (e.button.x - cx) +
                        (e.button.y - cy) * (e.button.y - cy);
          if (dist_sq < 40 * 40) {
            // Shrink back to pill position
            g_pill_fs.is_maximized = false;
            int pill_h = 40, pill_w = 260;
            int pill_x = g_window_width - pill_w - 30;
            int pill_y = 10;
            g_pill_fs.target_x = (float)pill_x;
            g_pill_fs.target_y = (float)pill_y;
            g_pill_fs.target_w = (float)pill_w;
            g_pill_fs.target_h = (float)pill_h;
          } else {
            // Check Cast Icon (Top Right)
            int tx = (int)wx + (int)ww - 50;
            int ty = (int)wy + 50;
            int t_dist_sq = (e.button.x - tx) * (e.button.x - tx) +
                            (e.button.y - ty) * (e.button.y - ty);
            if (t_dist_sq < 40 * 40) {
              start_cast_server();
              g_cast_popup_open = true;
              g_cast_scanning = true;
              g_cast_scan_start = SDL_GetTicks();
              send_discovery_queries();
              continue;
            }

            // Check Play/Pause Button in maximized mode
            float max_art_size = wh * 0.5f;
            float max_art_y_screen = (wh - max_art_size) / 2.0f;
            float max_text_x_screen = 80.0f + max_art_size + 60.0f;
            float seek_w = ww * 0.35f;
            if (max_text_x_screen + seek_w > ww - 50.0f)
              seek_w = ww - max_text_x_screen - 50.0f;
            if (seek_w < 100) seek_w = 100;

            float seek_x = wx + max_text_x_screen;
            float seek_y = wy + max_art_y_screen + 190.0f;
            if (e.button.x >= seek_x && e.button.x <= seek_x + seek_w &&
                e.button.y >= seek_y - 20 && e.button.y <= seek_y + 20) {
                float pct = (e.button.x - seek_x) / seek_w;
                float target_time = pct * g_current_track.duration;
                char cmd_buf[64];
                snprintf(cmd_buf, sizeof(cmd_buf), "SEEK %.2f", target_time);
                send_player_command(cmd_buf);
            } else {
                int btn_x = (int)(wx + max_text_x_screen + seek_w / 2.0f);
                int btn_y = (int)(wy + max_art_y_screen + 270.0f);
                int b_dist_sq = (e.button.x - btn_x) * (e.button.x - btn_x) +
                                (e.button.y - btn_y) * (e.button.y - btn_y);
                if (b_dist_sq < 50 * 50) {
                  g_is_playing = !g_is_playing;
                  send_player_command(g_is_playing ? "PLAY" : "PAUSE");
                  printf("[GUI] Play/Pause toggled: %s\n", g_is_playing ? "PLAY" : "PAUSE");
                }
            }
          }
          continue; // Consume event
        }

        // 1b. Priority: Grid Widget Maximized (separate from pill)
        if (g_music_widget.is_maximized) {
          float wx = g_music_widget.x, wy = g_music_widget.y;
          float ww = g_music_widget.w, wh = g_music_widget.h;

          // Check Chevron (Top Left) to minimize back to grid
          int cx = (int)wx + 50;
          int cy = (int)wy + 50;
          int dist_sq = (e.button.x - cx) * (e.button.x - cx) +
                        (e.button.y - cy) * (e.button.y - cy);
          if (dist_sq < 40 * 40) {
            g_music_widget.is_maximized = false;
            g_music_widget.target_w = g_music_widget.saved_w;
            g_music_widget.target_h = g_music_widget.saved_h;
            validate_widget_positions();
          } else {
            // Check Cast Icon (Top Right)
            int tx = (int)wx + (int)ww - 50;
            int ty = (int)wy + 50;
            int t_dist_sq = (e.button.x - tx) * (e.button.x - tx) +
                            (e.button.y - ty) * (e.button.y - ty);
            if (t_dist_sq < 40 * 40) {
              start_cast_server();
              g_cast_popup_open = true;
              g_cast_scanning = true;
              g_cast_scan_start = SDL_GetTicks();
              send_discovery_queries();
              continue;
            }

            float max_art_size = wh * 0.5f;
            float max_art_y_screen = (wh - max_art_size) / 2.0f;
            float max_text_x_screen = 80.0f + max_art_size + 60.0f;
            float seek_w = ww * 0.35f;
            if (max_text_x_screen + seek_w > ww - 50.0f)
              seek_w = ww - max_text_x_screen - 50.0f;
            if (seek_w < 100) seek_w = 100;

            float seek_x = wx + max_text_x_screen;
            float seek_y = wy + max_art_y_screen + 190.0f;
            if (e.button.x >= seek_x && e.button.x <= seek_x + seek_w &&
                e.button.y >= seek_y - 20 && e.button.y <= seek_y + 20) {
                float pct = (e.button.x - seek_x) / seek_w;
                float target_time = pct * g_current_track.duration;
                char cmd_buf[64];
                snprintf(cmd_buf, sizeof(cmd_buf), "SEEK %.2f", target_time);
                send_player_command(cmd_buf);
            } else {
                int btn_x = (int)(wx + max_text_x_screen + seek_w / 2.0f);
                int btn_y = (int)(wy + max_art_y_screen + 270.0f);
                int b_dist_sq = (e.button.x - btn_x) * (e.button.x - btn_x) +
                                (e.button.y - btn_y) * (e.button.y - btn_y);
                if (b_dist_sq < 50 * 50) {
                  g_is_playing = !g_is_playing;
                  send_player_command(g_is_playing ? "PLAY" : "PAUSE");
                  printf("[GUI] Play/Pause toggled (Pill): %s\n", g_is_playing ? "PLAY" : "PAUSE");
                }
            }
          }
          continue;
        }

        if (e.button.x < 60 && e.button.y < 60)
          g_sidebar_open = !g_sidebar_open;

        // Pill Widget Hit — triggers its OWN fullscreen overlay, never touches g_music_widget
        if (g_song_selected && g_pill_hover) {
            int pill_h = 40;
            int pill_w = 260;
            int pill_x = g_window_width - pill_w - 30;
            int pill_y = 10;

            g_pill_fs.active = true;
            g_pill_fs.is_maximized = true;
            g_pill_fs.x = (float)pill_x;
            g_pill_fs.y = (float)pill_y;
            g_pill_fs.w = (float)pill_w;
            g_pill_fs.h = (float)pill_h;
            g_pill_fs.target_x = 0;
            g_pill_fs.target_y = 0;
            g_pill_fs.target_w = (float)g_window_width;
            g_pill_fs.target_h = (float)g_window_height;
            continue;
        }

        // Widget Hit (Music Player) - Grid widget only, no pill involvement
        float wx = g_music_widget.x, wy = g_music_widget.y;
        float ww = g_music_widget.w, wh = g_music_widget.h;

        // Account for rotation (sway physics) in hitbox detection
        float cx = wx + ww / 2.0f;
        float cy = wy + wh / 2.0f;
        float ux = (float)e.button.x;
        float uy = (float)e.button.y;

        if (g_music_widget.rotation != 0.0f) {
            float rad = -g_music_widget.rotation * (M_PI / 180.0f);
            float s = sinf(rad);
            float c = cosf(rad);
            float tx = ux - cx;
            float ty = uy - cy;
            ux = tx * c - ty * s + cx;
            uy = tx * s + ty * c + cy;
        }

        if (ux > wx && ux < wx + ww && uy > wy && uy < wy + wh) {
          // Check for Resize Handle (Right edge, 30px margin)
          if (ux > wx + ww - 30) {
            g_music_widget.is_resizing = true;
          } else {
            // Check if clicking controls
            bool show_side_controls = (ww > 600);
            int btn_x, btn_y;
            if (show_side_controls) {
              btn_x = (int)wx + (int)ww - 70;
              btn_y = (int)wy + (int)wh / 2;
            } else {
              btn_x = (int)wx + 25 + ((int)wh - 50) / 2;
              btn_y = (int)wy + 25 + ((int)wh - 50) / 2;
            }

            int dist_sq = (ux - btn_x) * (ux - btn_x) +
                          (uy - btn_y) * (uy - btn_y);
            if (dist_sq < 35 * 35) {
              g_is_playing = !g_is_playing;
              send_player_command(g_is_playing ? "PLAY" : "PAUSE");
            } else {
              // Check for Drag Handle (Top center bar area)
              SDL_Rect drag_area = {(int)wx + (int)ww / 2 - 40, (int)wy, 80, 30};
              if (ux > drag_area.x &&
                  ux < drag_area.x + drag_area.w &&
                  uy > drag_area.y &&
                  uy < drag_area.y + drag_area.h) {
                g_music_widget.is_dragging = true;
                g_music_widget.was_dragged = false;
                g_music_widget.drag_off_x = (float)ux - wx;
                g_music_widget.drag_off_y = (float)uy - wy;
              }
            }
          }
        }

        if (g_sidebar_open && e.button.x > 15 && e.button.x < SIDEBAR_WIDTH - 15) {
          // Home Tab Click
          if (e.button.y > TOP_BAR_HEIGHT + 75 && e.button.y < TOP_BAR_HEIGHT + 125) {
            if (g_current_tab != TAB_HOME) {
              g_prev_header[0] = 0;
              strcpy(g_prev_header, g_display_header);
              strcpy(g_display_header, "Welcome Home");
              g_current_tab = TAB_HOME;
              g_header_transition = 0.0f;
              g_is_transitioning = true;
            }
          }
          // Music Tab Click
          if (e.button.y > TOP_BAR_HEIGHT + 130 && e.button.y < TOP_BAR_HEIGHT + 180) {
            if (g_current_tab != TAB_MUSIC) {
              g_prev_header[0] = 0;
              strcpy(g_prev_header, g_display_header);
              strcpy(g_display_header, "Music");
              g_current_tab = TAB_MUSIC;
              g_header_transition = 0.0f;
              g_is_transitioning = true;
            }
            
            // Original launch logic
            bool term_hover = g_player_running && (e.button.x > SIDEBAR_WIDTH - 15 - 95 && e.button.x < SIDEBAR_WIDTH - 15 &&
                                                   e.button.y > TOP_BAR_HEIGHT + 130 && e.button.y < TOP_BAR_HEIGHT + 180);
            if (!term_hover) {
              if (e.button.clicks == 2)
                launch_player_process(NULL, false);
              else if (!g_player_running)
                launch_player_process(NULL, true);
            }
          }
        }

        int search_w = 400;
        int search_x = (g_window_width - search_w) / 2;

        if (e.button.x > search_x && e.button.x < search_x + search_w && e.button.y > 10 &&
            e.button.y < 50) {
          if (g_search_query[0] == '/' ||
              (g_search_query[0] == '.' && g_search_query[1] == '/')) {
            launch_player_process(g_search_query, true);
            g_search_query[0] = 0;
          }
        }

        if (g_result_count > 0 && e.button.x > search_x && e.button.x < search_x + search_w &&
            e.button.y > 60) {
          int i = (e.button.y - 65) / 50;
          if (i >= 0 && i < g_result_count) {
            launch_player_process(g_results[i].filepath, true);
            g_search_query[0] = 0;
            g_result_count = 0;
          }
        }
      }
      if (e.type == SDL_MOUSEBUTTONUP) {
        if (g_music_widget.is_resizing) {
          g_music_widget.is_resizing = false;
          // Snap to stages
          if (g_music_widget.w > 630)
            g_music_widget.target_w = 860;
          else if (g_music_widget.w > 280)
            g_music_widget.target_w = 400;
          else
            g_music_widget.target_w = 160;
        }
        if (g_music_widget.is_dragging) {
          g_music_widget.is_dragging = false;
          if (!g_music_widget.was_dragged) {
            g_music_widget.is_maximized = true;
            g_music_widget.saved_w = g_music_widget.target_w;
            g_music_widget.saved_h = g_music_widget.target_h;
            validate_widget_positions();
          } else {
            int col, row;
            get_nearest_grid_slot(g_music_widget.x, g_music_widget.y, &col, &row);
            
            // Rule 2: Store user placement
            g_music_widget.grid_col = col;
            g_music_widget.grid_row = row;
            
            validate_widget_positions();
          }
        }
      }
    }

    // Smooth Transitions & Sway Physics
    static float prev_x = 20.0f;

    // Tab Indicator smooth movement
    float target_indic_y = (g_current_tab == TAB_HOME) ? (TOP_BAR_HEIGHT + 75) : (TOP_BAR_HEIGHT + 130);
    g_tab_indicator_y += (target_indic_y - g_tab_indicator_y) * 0.15f;

    // Header transition logic
    if (g_is_transitioning) {
        g_header_transition += 0.04f;
        if (g_header_transition >= 1.0f) {
            g_header_transition = 1.0f;
            g_is_transitioning = false;
        }
    }

    if (!g_music_widget.is_dragging) {
      g_music_widget.x += (g_music_widget.target_x - g_music_widget.x) * 0.18f;
      g_music_widget.y += (g_music_widget.target_y - g_music_widget.y) * 0.18f;
    }
    g_music_widget.w += (g_music_widget.target_w - g_music_widget.w) * 0.18f;
    g_music_widget.h += (g_music_widget.target_h - g_music_widget.h) * 0.18f;

    // Pill Fullscreen animation (completely independent from grid widget)
    if (g_pill_fs.active) {
        g_pill_fs.x += (g_pill_fs.target_x - g_pill_fs.x) * 0.18f;
        g_pill_fs.y += (g_pill_fs.target_y - g_pill_fs.y) * 0.18f;
        g_pill_fs.w += (g_pill_fs.target_w - g_pill_fs.w) * 0.18f;
        g_pill_fs.h += (g_pill_fs.target_h - g_pill_fs.h) * 0.18f;

        // Handoff: if shrinking back to pill and almost there, deactivate overlay
        if (!g_pill_fs.is_maximized) {
            float dx = g_pill_fs.x - g_pill_fs.target_x;
            float dy = g_pill_fs.y - g_pill_fs.target_y;
            float dw = g_pill_fs.w - g_pill_fs.target_w;
            if (dx*dx + dy*dy < 10.0f && dw*dw < 10.0f) {
                g_pill_fs.active = false;
            }
        }
    }

    // Sway Physics (Rotation based on horizontal velocity)
    float dx = g_music_widget.x - prev_x;
    prev_x = g_music_widget.x;

    g_music_widget.target_rotation = g_music_widget.is_maximized ? 0.0f : dx * 1.8f;
    // Disable sway when returning from maximized — keep it clean during transition
    if (!g_music_widget.is_maximized && (fabsf(g_music_widget.w - g_music_widget.target_w) > 5.0f ||
                                          fabsf(g_music_widget.h - g_music_widget.target_h) > 5.0f)) {
        g_music_widget.target_rotation = 0.0f;
    }
    if (g_music_widget.target_rotation > 12.0f)
      g_music_widget.target_rotation = 12.0f;
    if (g_music_widget.target_rotation < -12.0f)
      g_music_widget.target_rotation = -12.0f;
    g_music_widget.rotation +=
        (g_music_widget.target_rotation - g_music_widget.rotation) * 0.15f;

    char z_buf[MAX_LOG_LINE];
    if (zmq_recv(z_sock, z_buf, sizeof(z_buf) - 1, ZMQ_DONTWAIT) > 0) {
      char *t = strstr(z_buf, "title\": \"");
      if (t) {
        t += 9;
        char *end = strchr(t, '\"');
        if (end) {
          int len = end - t;
          strncpy(g_current_track.title, t, len);
          g_current_track.title[len] = 0;
          g_song_selected = true;
        }
      }
      char *a = strstr(z_buf, "artist\": \"");
      if (a) {
        a += 10;
        char *end = strchr(a, '\"');
        if (end) {
          int len = end - a;
          strncpy(g_current_track.artist, a, len);
          g_current_track.artist[len] = 0;
        }
      }
      char *art = strstr(z_buf, "art_path\": \"");
      if (art) {
        art += 12;
        char *end = strchr(art, '\"');
        if (end) {
          int len = end - art;
          strncpy(g_current_track.art_path, art, len);
          g_current_track.art_path[len] = 0;
        }
      }
      char *st = strstr(z_buf, "state\": \"");
      if (st)
        g_is_playing = (strncmp(st + 9, "playing", 7) == 0);
      
      char *pos = strstr(z_buf, "position\": ");
      if (pos) {
          g_current_track.position = atof(pos + 11);
      }
      char *dur = strstr(z_buf, "duration\": ");
      if (dur) {
          g_current_track.duration = atof(dur + 11);
      }

      char *col = strstr(z_buf, "theme_color\": \"");
      if (col) {
        col += 15;
        if (col[0] == '#') {
          unsigned int r_val, g_val, b_val;
          if (sscanf(col + 1, "%02x%02x%02x", &r_val, &g_val, &b_val) == 3) {
            g_theme.target_accent.r = r_val;
            g_theme.target_accent.g = g_val;
            g_theme.target_accent.b = b_val;
          }
        }
      }
      save_hub_state();
      zmq_send(z_sock, "ACK", 3, 0);
    }

    // Cast connection watchdog
    if (g_cast_client_connected) {
      if (SDL_GetTicks() - g_cast_last_poll > 5000) { // 5 second timeout
        g_cast_client_connected = false;
        send_player_command("VOLUME 100"); // Restore volume
        show_toast("Cast connection lost. Resuming locally.", 3000);
      }
    }

    if (g_song_selected)
      update_art_cache(ren, g_current_track.art_path);

    // Color interpolation
    g_theme.accent.r += (g_theme.target_accent.r - g_theme.accent.r) * 0.05f;
    g_theme.accent.g += (g_theme.target_accent.g - g_theme.accent.g) * 0.05f;
    g_theme.accent.b += (g_theme.target_accent.b - g_theme.accent.b) * 0.05f;

    if (g_sidebar_open && g_sidebar_anim < 1.0f)
      g_sidebar_anim += 0.1f;
    if (!g_sidebar_open && g_sidebar_anim > 0.0f)
      g_sidebar_anim -= 0.1f;

    SDL_SetRenderDrawColor(ren, g_theme.bg.r, g_theme.bg.g, g_theme.bg.b, 255);
    SDL_RenderClear(ren);

    // 1. Playmat Background (Behind everything)
    // No special background for now, just the theme bg

    // 2. Header Container (Below Top Bar)
    SDL_Rect header_rect = {0, TOP_BAR_HEIGHT, g_window_width, HEADER_HEIGHT};
    draw_vertical_gradient(ren, header_rect, g_theme.top_bar, g_theme.bg);
    
    if (g_is_transitioning) {
        render_hero_header(ren, g_prev_header, g_window_width / 2, 
                           TOP_BAR_HEIGHT + HEADER_HEIGHT / 2 + 20, 
                           g_header_transition, true);
        render_hero_header(ren, g_display_header, g_window_width / 2, 
                           TOP_BAR_HEIGHT + HEADER_HEIGHT / 2 + 20, 
                           g_header_transition, false);
    } else {
        render_hero_header(ren, g_display_header, g_window_width / 2, 
                           TOP_BAR_HEIGHT + HEADER_HEIGHT / 2 + 20, 
                           1.0f, false);
    }

    // 3. Playmat (Widgets)
    bool draw_widget_late = (g_music_widget.h > 200 || g_music_widget.is_maximized);
    bool draw_pill_fs = g_pill_fs.active;

    if (g_music_widget.is_dragging) {
      float wcx = g_music_widget.x + g_music_widget.w / 2.0f;
      float wcy = g_music_widget.y + g_music_widget.h / 2.0f;
      render_grid_drop_guides(ren, wcx, wcy);
    }

    if (!draw_widget_late && (g_current_tab == TAB_MUSIC || g_music_widget.is_maximized) && g_music_widget.active) {
      RENDER_MUSIC_WIDGET_MACRO();
    }

    // 4. Sidebar (Above Playmat/Header, Below Top Bar)
    if (g_sidebar_anim > 0.01f) {
      int sx = (int)((g_sidebar_anim - 1) * SIDEBAR_WIDTH);
      SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
      SDL_Rect sb = {sx, TOP_BAR_HEIGHT, SIDEBAR_WIDTH, g_window_height - TOP_BAR_HEIGHT};
      SDL_RenderFillRect(ren, &sb);
      
      if (!g_settings_open) {
        // Navigation Header
        render_text(ren, "Navigation", sx + 30, TOP_BAR_HEIGHT + 45, g_theme.accent);
        
        // Active Tab Indicator (The smooth pill)
        SDL_Rect indic = {sx + 15, (int)g_tab_indicator_y, SIDEBAR_WIDTH - 30, 50};
        
        // Subtle Glow behind pill
        SDL_Color glow_col = g_theme.accent;
        glow_col.a = 40;
        fill_rounded_rect_hq(ren, indic.x - 2, indic.y - 2, indic.w + 4, indic.h + 4, 14, glow_col);

        fill_rounded_rect_hq(ren, indic.x, indic.y, indic.w, indic.h, 12, (SDL_Color){45, 45, 45, 255});
        draw_rounded_outline_hq(ren, indic.x, indic.y, indic.w, indic.h, 12, 1, g_theme.accent);

        // Home Item
        SDL_Rect home_rect = {sx + 15, TOP_BAR_HEIGHT + 75, SIDEBAR_WIDTH - 30, 50};
        render_text(ren, "Home", sx + 30, home_rect.y + 32, (g_current_tab == TAB_HOME) ? g_theme.text_main : g_theme.text_dim);

        // Music Player Item
        SDL_Rect item_rect = {sx + 15, TOP_BAR_HEIGHT + 130, SIDEBAR_WIDTH - 30, 50};
        render_text(ren, "Music Player", sx + 30, item_rect.y + 32, (g_current_tab == TAB_MUSIC) ? g_theme.text_main : g_theme.text_dim);
        
        // Status Pill with hover-to-terminate animation
        if (g_player_running) {
          static float term_anim = 0.0f;
          int base_w = 65;
          int expand_w = 95; // Enough for "Terminate"
          
          // Hover area for the pill
          SDL_Rect hover_rect = {sx + SIDEBAR_WIDTH - 15 - expand_w, item_rect.y, expand_w, 50};
          bool hov_pill = (g_mouse_x > hover_rect.x && g_mouse_x < hover_rect.x + hover_rect.w &&
                           g_mouse_y > hover_rect.y && g_mouse_y < hover_rect.y + hover_rect.h);
          
          if (hov_pill) {
             term_anim += 0.08f;
             if (term_anim > 1.0f) term_anim = 1.0f;
          } else {
             term_anim -= 0.08f;
             if (term_anim < 0.0f) term_anim = 0.0f;
          }
          
          int cur_w = base_w + (int)((expand_w - base_w) * term_anim);
          SDL_Rect pill = {sx + SIDEBAR_WIDTH - 15 - cur_w, item_rect.y + 12, cur_w, 26};
          
          // Color interpolation (Green to Red)
          int r = 20 + (int)((180 - 20) * term_anim);
          int g = 40 + (int)((30 - 40) * term_anim);
          int b = 20 + (int)((30 - 20) * term_anim);
          
          int br = 100 + (int)((255 - 100) * term_anim);
          int bg = 255 + (int)((80 - 255) * term_anim);
          int bb = 100 + (int)((80 - 100) * term_anim);

          fill_rounded_rect_hq(ren, pill.x, pill.y, pill.w, pill.h, 13, (SDL_Color){r, g, b, 255});
          draw_rounded_outline_hq(ren, pill.x, pill.y, pill.w, pill.h, 13, 1, (SDL_Color){br, bg, bb, 150});
          
          // Text transition
          if (term_anim < 0.5f) {
             float alpha = 1.0f - (term_anim * 2.0f);
             render_text_scaled(ren, "Active", pill.x + 12, pill.y + 19, 0.85f, (SDL_Color){100, 255, 100, (uint8_t)(255 * alpha)});
          } else {
             float alpha = (term_anim - 0.5f) * 2.0f;
             render_text_scaled(ren, "Terminate?", pill.x + 10, pill.y + 19, 0.85f, (SDL_Color){255, 200, 200, (uint8_t)(255 * alpha)});
          }
          
          if (hov_pill && (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT))) {
             static uint32_t last_term_click = 0;
             if (SDL_GetTicks() - last_term_click > 500) {
                 send_player_command("QUIT");
                 system("pkill -9 harmony_player");
                 g_player_running = false;
                 g_song_selected = false;
                 show_toast("Terminating Player...", 2000);
                 last_term_click = SDL_GetTicks();
                 term_anim = 0.0f;
             }
          }
        } else {
          render_text_scaled(ren, "Offline", sx + 160, item_rect.y + 32, 0.85f, (SDL_Color){150, 150, 150, 200});
        }

        // Settings Entry at the bottom
        SDL_Rect set_btn = {sx + 20, g_window_height - 70, SIDEBAR_WIDTH - 40, 50};
        bool hov_set = (g_mouse_x > set_btn.x && g_mouse_x < set_btn.x + set_btn.w &&
                         g_mouse_y > set_btn.y && g_mouse_y < set_btn.y + set_btn.h);
        fill_rounded_rect_hq(ren, set_btn.x, set_btn.y, set_btn.w, set_btn.h, 12, 
                             hov_set ? (SDL_Color){50, 50, 50, 255} : (SDL_Color){35, 35, 35, 255});
        draw_rounded_outline_hq(ren, set_btn.x, set_btn.y, set_btn.w, set_btn.h, 12, 1, (SDL_Color){60, 60, 60, 255});
        render_text(ren, "Settings  ⚙", set_btn.x + 45, set_btn.y + 32, g_theme.text_main);
        
        if (hov_set && SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)) {
           g_settings_open = true;
        }
      } else {
        // Settings Menu View
        render_text(ren, "← Back", sx + 25, TOP_BAR_HEIGHT + 40, g_theme.text_dim);
        if (g_mouse_x > sx + 20 && g_mouse_x < sx + 100 && g_mouse_y > TOP_BAR_HEIGHT + 15 && g_mouse_y < TOP_BAR_HEIGHT + 55) {
          if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)) g_settings_open = false;
        }

        render_text(ren, "Settings", sx + 30, TOP_BAR_HEIGHT + 85, g_theme.accent);
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderDrawLine(ren, sx + 25, TOP_BAR_HEIGHT + 100, sx + SIDEBAR_WIDTH - 25, TOP_BAR_HEIGHT + 100);

        // Privacy Section
        render_text(ren, "Privacy & Data", sx + 30, TOP_BAR_HEIGHT + 135, g_theme.text_main);
        render_text_scaled(ren, "• Local-only storage", sx + 35, TOP_BAR_HEIGHT + 160, 0.9f, g_theme.text_dim);
        render_text_scaled(ren, "• No telemetry", sx + 35, TOP_BAR_HEIGHT + 180, 0.9f, g_theme.text_dim);

        SDL_Rect clear_btn = {sx + 30, TOP_BAR_HEIGHT + 205, 190, 42};
        bool hover_clear = (g_mouse_x > clear_btn.x && g_mouse_x < clear_btn.x + clear_btn.w &&
                            g_mouse_y > clear_btn.y && g_mouse_y < clear_btn.y + clear_btn.h);
        fill_rounded_rect_hq(ren, clear_btn.x, clear_btn.y, clear_btn.w, clear_btn.h, 10, 
                             hover_clear ? (SDL_Color){80, 40, 40, 255} : (SDL_Color){45, 45, 45, 255});
        render_text_scaled(ren, "Clear App History", clear_btn.x + 18, clear_btn.y + 28, 0.95f, g_theme.text_main);
        
        if (hover_clear && SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)) {
           clear_privacy_data();
        }

        // About Section
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderDrawLine(ren, sx + 25, TOP_BAR_HEIGHT + 275, sx + SIDEBAR_WIDTH - 25, TOP_BAR_HEIGHT + 275);
        render_text(ren, "About Harmony", sx + 30, TOP_BAR_HEIGHT + 310, g_theme.text_main);
        render_text_scaled(ren, "Version 2.1.0-alpha", sx + 30, TOP_BAR_HEIGHT + 340, 0.85f, g_theme.text_dim);
        render_text_scaled(ren, "Research Preview", sx + 30, TOP_BAR_HEIGHT + 360, 0.85f, g_theme.text_dim);
        render_text_scaled(ren, "Built with ZMQ & SDL2", sx + 30, TOP_BAR_HEIGHT + 380, 0.85f, g_theme.text_dim);
      }
    }

    // 5. Top Bar (Always on Top... unless widgets float over)
    SDL_SetRenderDrawColor(ren, g_theme.top_bar.r, g_theme.top_bar.g,
                           g_theme.top_bar.b, 255);
    SDL_Rect top = {0, 0, g_window_width, TOP_BAR_HEIGHT};
    SDL_RenderFillRect(ren, &top);
    
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    for (int i = 0; i < 3; i++) {
      SDL_Rect l = {20, 20 + i * 10, 30, 3};
      SDL_RenderFillRect(ren, &l);
    }

    int pill_w = 260;
    int pill_x = g_window_width - pill_w - 30;

    int search_w = 400;
    if (search_w > pill_x - 100) search_w = pill_x - 100;
    int search_x = (pill_x + 60 - search_w) / 2; // Center between menu and pill

    SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
    SDL_Rect srch = {search_x, 10, search_w, 40};
    SDL_RenderFillRect(ren, &srch);
    render_text(ren, strlen(g_search_query) ? g_search_query : "Search Shit...",
                search_x + 10, 35, g_theme.text_dim);

    render_now_playing_pill(ren, g_mouse_x, g_mouse_y);

    if (g_result_count > 0) {
      SDL_SetRenderDrawColor(ren, 30, 30, 30, 240);
      SDL_Rect res_bg = {search_x, 60, search_w, g_result_count * 50};
      SDL_RenderFillRect(ren, &res_bg);
      for (int i = 0; i < g_result_count; i++)
        render_text(ren, g_results[i].title, search_x + 15, 95 + i * 50,
                    g_theme.text_main);
    }

    // 6. Maximized Widgets (Drawn late)
    if (draw_widget_late && g_music_widget.active) {
      RENDER_MUSIC_WIDGET_MACRO();
    }

    // 6b. Pill Fullscreen Overlay (drawn on top of everything)
    if (draw_pill_fs) {
      RENDER_PILL_FS_MACRO();
    }

    // 7. Cast Popup (drawn above everything except toasts/modals)
    render_cast_popup(ren);

    // 8. Toast Notifications (System Transparency)
    if (g_toast.active) {
      uint32_t elapsed = SDL_GetTicks() - g_toast.start_time;
      if (elapsed > g_toast.duration) {
        g_toast.active = false;
      } else {
        float alpha = 1.0f;
        if (elapsed > g_toast.duration - 500) alpha = (g_toast.duration - elapsed) / 500.0f;
        if (elapsed < 500) alpha = elapsed / 500.0f;
        
        int tw = 350, th = 50;
        int tx = (g_window_width - tw) / 2;
        int ty = g_window_height - 80;
        SDL_Color t_bg = {40, 40, 40, (uint8_t)(230 * alpha)};
        fill_rounded_rect_hq(ren, tx, ty, tw, th, 12, t_bg);
        SDL_Color t_txt = {255, 255, 255, (uint8_t)(255 * alpha)};
        render_text(ren, g_toast.message, tx + 20, ty + 32, t_txt);
      }
    }

    // 8. Modals (Ethical Interventions / Destructive Actions)
    if (g_modal.active) {
      // Dim background
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
      SDL_Rect screen = {0, 0, g_window_width, g_window_height};
      SDL_RenderFillRect(ren, &screen);
      
      int mw = 500, mh = 260;
      int mx = (g_window_width - mw) / 2;
      int my = (g_window_height - mh) / 2;
      
      fill_rounded_rect_hq(ren, mx, my, mw, mh, 20, (SDL_Color){30, 30, 30, 255});
      draw_rounded_outline_hq(ren, mx, my, mw, mh, 20, 2, g_modal.danger ? (SDL_Color){255, 80, 80, 255} : g_theme.accent);
      
      render_text(ren, g_modal.title, mx + 30, my + 50, g_modal.danger ? (SDL_Color){255, 80, 80, 255} : g_theme.text_main);
      
      // Simple word wrap for modal description
      char desc_copy[256];
      strncpy(desc_copy, g_modal.description, 255);
      char *words[64];
      int word_count = 0;
      char *w_ptr = strtok(desc_copy, " ");
      while (w_ptr && word_count < 64) {
        words[word_count++] = w_ptr;
        w_ptr = strtok(NULL, " ");
      }
      
      char line[128] = "";
      int line_y = my + 95;
      for (int i = 0; i < word_count; i++) {
        char next_line[128];
        snprintf(next_line, 127, "%s%s ", line, words[i]);
        if (get_text_width_scaled(next_line, 1.0f) > mw - 60) {
          render_text(ren, line, mx + 30, line_y, g_theme.text_dim);
          line_y += 30;
          strncpy(line, words[i], 127);
          strncat(line, " ", 127);
        } else {
          strncpy(line, next_line, 127);
        }
      }
      render_text(ren, line, mx + 30, line_y, g_theme.text_dim);
      
      // Cancel Button (Wider: 140px)
      SDL_Rect btn_can = {mx + mw - 310, my + mh - 65, 140, 45};
      bool hov_can = (g_mouse_x > btn_can.x && g_mouse_x < btn_can.x + btn_can.w &&
                      g_mouse_y > btn_can.y && g_mouse_y < btn_can.y + btn_can.h);
      fill_rounded_rect_hq(ren, btn_can.x, btn_can.y, btn_can.w, btn_can.h, 10, hov_can ? (SDL_Color){60, 60, 60, 255} : (SDL_Color){45, 45, 45, 255});
      render_text(ren, g_modal.cancel_label, btn_can.x + 35, btn_can.y + 30, g_theme.text_main);
      
      // Confirm Button (Wider: 140px)
      SDL_Rect btn_conf = {mx + mw - 150, my + mh - 65, 140, 45};
      bool hov_conf = (g_mouse_x > btn_conf.x && g_mouse_x < btn_conf.x + btn_conf.w &&
                       g_mouse_y > btn_conf.y && g_mouse_y < btn_conf.y + btn_conf.h);
      fill_rounded_rect_hq(ren, btn_conf.x, btn_conf.y, btn_conf.w, btn_conf.h, 10, hov_conf ? (g_modal.danger ? (SDL_Color){220, 60, 60, 255} : (SDL_Color){0, 220, 255, 255}) : (g_modal.danger ? (SDL_Color){180, 40, 40, 255} : g_theme.accent));
      render_text(ren, g_modal.confirm_label, btn_conf.x + 15, btn_conf.y + 30, (SDL_Color){255, 255, 255, 255});
    }

    SDL_RenderPresent(ren);
    if (g_should_quit) running = false;
    SDL_Delay(16);
  }

  if (g_player_running)
    send_player_command("QUIT");
  if (g_player_pid > 0) {
    kill(g_player_pid, SIGTERM);
    waitpid(g_player_pid, NULL, 0);
  }
  zmq_close(z_sock);
  zmq_close(g_z_cmd_sock);
  zmq_ctx_destroy(z_ctx);
  if (g_widget_tex)
    SDL_DestroyTexture(g_widget_tex);
  return 0;
}
