#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <zmq.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define WINDOW_WIDTH 900
#define WINDOW_HEIGHT 600
#define SIDEBAR_WIDTH 250
#define TOPBAR_HEIGHT 70
#define FONT_PATH "/mnt/mass-storage/Archive/Documents/dev/Harmony_Retooled/assets/fonts/Roboto-Regular.ttf"
#define DB_PATH "/mnt/mass-storage/Archive/Documents/dev/Harmony_Retooled/harmony_v2.db"

typedef struct {
    char title[256];
    char artist[256];
    char filepath[512];
} SearchResult;

// Global State
bool g_running = true;
bool g_sidebar_open = false;
float g_sidebar_anim = 0.0f; 
char g_search_query[256] = "";
char g_current_song[256] = "Not Playing";
char g_current_artist[256] = "";
bool g_player_running = false;
SearchResult g_results[10];
int g_result_count = 0;

SDL_Window *g_window = NULL;
SDL_Renderer *g_renderer = NULL;
TTF_Font *g_font_sm = NULL;
TTF_Font *g_font_md = NULL;
TTF_Font *g_font_lg = NULL;

void *g_zmq_context = NULL;
void *g_zmq_socket = NULL;

// Helper to check if player is running
void check_player_status() {
    g_player_running = (system("pgrep -x harmony_player > /dev/null") == 0);
}

// Helper to draw a rounded rectangle (simplified)
void draw_rounded_rect(SDL_Renderer *r, int x, int y, int w, int h, int radius, SDL_Color color) {
    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect); // Simplified for this barebones version
}

// Search database
void search_db(const char *query) {
    if (strlen(query) < 2) {
        g_result_count = 0;
        return;
    }
    sqlite3 *db;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) return;

    const char *sql = "SELECT t.title, a.name, t.filepath FROM tracks t "
                      "LEFT JOIN artists a ON t.artist_id = a.id "
                      "WHERE t.title LIKE ? OR a.name LIKE ? LIMIT 5;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    char pattern[260];
    snprintf(pattern, sizeof(pattern), "%%%s%%", query);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);

    g_result_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && g_result_count < 10) {
        strncpy(g_results[g_result_count].title, (const char*)sqlite3_column_text(stmt, 0), 255);
        strncpy(g_results[g_result_count].artist, (const char*)sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : "Unknown", 255);
        strncpy(g_results[g_result_count].filepath, (const char*)sqlite3_column_text(stmt, 2), 511);
        g_result_count++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void launch_player(const char *file) {
    char cmd[1024];
    if (file) {
        snprintf(cmd, sizeof(cmd), "cd /mnt/mass-storage/Archive/Documents/dev/Harmony_Retooled && ./harmony_player \"%s\" &", file);
    } else {
        snprintf(cmd, sizeof(cmd), "cd /mnt/mass-storage/Archive/Documents/dev/Harmony_Retooled && ./harmony_player &");
    }
    system(cmd);
}

void draw_text(const char *text, int x, int y, SDL_Color color, TTF_Font *font) {
    if (!text || text[0] == '\0') return;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (surf) {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(g_renderer, surf);
        SDL_Rect dst = {x, y, surf->w, surf->h};
        SDL_RenderCopy(g_renderer, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}

void render() {
    // Background
    SDL_SetRenderDrawColor(g_renderer, 18, 18, 18, 255);
    SDL_RenderClear(g_renderer);

    // Title Bar
    SDL_SetRenderDrawColor(g_renderer, 25, 25, 25, 255);
    SDL_Rect top_bar = {0, 0, WINDOW_WIDTH, TOPBAR_HEIGHT};
    SDL_RenderFillRect(g_renderer, &top_bar);

    // Hamburger Button
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
    for(int i=0; i<3; i++) {
        SDL_Rect line = {20, 25 + (i*10), 30, 4};
        SDL_RenderFillRect(g_renderer, &line);
    }

    // Search Bar
    SDL_SetRenderDrawColor(g_renderer, 40, 40, 40, 255);
    SDL_Rect search_rect = {WINDOW_WIDTH/2 - 200, 15, 400, 40};
    SDL_RenderFillRect(g_renderer, &search_rect);
    
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray = {150, 150, 150, 255};
    if (strlen(g_search_query) > 0) {
        draw_text(g_search_query, WINDOW_WIDTH/2 - 190, 22, white, g_font_md);
    } else {
        draw_text("Search Music Library...", WINDOW_WIDTH/2 - 190, 22, gray, g_font_md);
    }

    // Main Body - Now Playing
    draw_text("Now Playing", 100, 150, gray, g_font_md);
    draw_text(g_current_song, 100, 200, white, g_font_lg);
    draw_text(g_current_artist, 100, 260, gray, g_font_md);

    // Search Results Dropdown
    if (g_result_count > 0) {
        SDL_SetRenderDrawColor(g_renderer, 35, 35, 35, 255);
        SDL_Rect res_bg = {WINDOW_WIDTH/2 - 200, 55, 400, g_result_count * 50};
        SDL_RenderFillRect(g_renderer, &res_bg);
        for (int i=0; i<g_result_count; i++) {
            draw_text(g_results[i].title, WINDOW_WIDTH/2 - 190, 65 + (i*50), white, g_font_sm);
        }
    }

    // Sidebar
    int sb_x = (int)((g_sidebar_anim - 1.0f) * SIDEBAR_WIDTH);
    if (g_sidebar_anim > 0.01f) {
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 150); // Dim overlay
        SDL_Rect overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
        SDL_RenderFillRect(g_renderer, &overlay);

        SDL_SetRenderDrawColor(g_renderer, 24, 24, 24, 255);
        SDL_Rect sb_rect = {sb_x, 0, SIDEBAR_WIDTH, WINDOW_HEIGHT};
        SDL_RenderFillRect(g_renderer, &sb_rect);

        SDL_Color item_color = g_player_running ? white : gray;
        draw_text("Music", sb_x + 30, 100, item_color, g_font_md);
        if (!g_player_running) {
            draw_text("(Launch Player)", sb_x + 30, 130, gray, g_font_sm);
        }
    }

    SDL_RenderPresent(g_renderer);
}

int main(int argc, char *argv[]) {
    setenv("HARMONY_ENGINE", "1", 1);
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    if (TTF_Init() < 0) return 1;

    g_window = SDL_CreateWindow("EngineP Hub", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    g_font_sm = TTF_OpenFont(FONT_PATH, 16);
    g_font_md = TTF_OpenFont(FONT_PATH, 20);
    g_font_lg = TTF_OpenFont(FONT_PATH, 48);

    g_zmq_context = zmq_ctx_new();
    g_zmq_socket = zmq_socket(g_zmq_context, ZMQ_REP);
    zmq_bind(g_zmq_socket, "ipc:///tmp/hub_socket.ipc");

    SDL_Event e;
    Uint32 last_status_check = 0;

    while (g_running) {
        Uint32 now = SDL_GetTicks();
        if (now - last_status_check > 2000) {
            check_player_status();
            last_status_check = now;
        }

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) g_running = false;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_BACKSPACE && strlen(g_search_query) > 0) {
                    g_search_query[strlen(g_search_query)-1] = '\0';
                    search_db(g_search_query);
                }
            }
            if (e.type == SDL_TEXTINPUT) {
                strncat(g_search_query, e.text.text, sizeof(g_search_query) - strlen(g_search_query) - 1);
                search_db(g_search_query);
            }
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x;
                int my = e.button.y;
                // Hamburger hit
                if (mx < 60 && my < 60) g_sidebar_open = !g_sidebar_open;
                // Sidebar item hit
                if (g_sidebar_open && mx < SIDEBAR_WIDTH && my > 90 && my < 160) {
                    if (!g_player_running) launch_player(NULL);
                }
                // Result hit
                if (g_result_count > 0 && mx > WINDOW_WIDTH/2 - 200 && mx < WINDOW_WIDTH/2 + 200 && my > 55) {
                    int idx = (my - 55) / 50;
                    if (idx >= 0 && idx < g_result_count) {
                        launch_player(g_results[idx].filepath);
                        g_search_query[0] = '\0';
                        g_result_count = 0;
                    }
                }
            }
        }

        // ZMQ Poll (non-blocking)
        zmq_msg_t msg;
        zmq_msg_init(&msg);
        int bytes = zmq_msg_recv(&msg, g_zmq_socket, ZMQ_DONTWAIT);
        if (bytes > 0) {
            char *data = zmq_msg_data(&msg);
            // Very basic JSON-ish parsing for the demo
            if (strstr(data, "song_started")) {
                char *title = strstr(data, "title\": \"");
                if (title) {
                    title += 9;
                    char *end = strchr(title, '\"');
                    if (end) {
                        int len = end - title;
                        strncpy(g_current_song, title, len);
                        g_current_song[len] = '\0';
                    }
                }
            }
            zmq_send(g_zmq_socket, "ACK", 3, 0);
        }
        zmq_msg_close(&msg);

        // Sidebar animation
        if (g_sidebar_open && g_sidebar_anim < 1.0f) g_sidebar_anim += 0.1f;
        if (!g_sidebar_open && g_sidebar_anim > 0.0f) g_sidebar_anim -= 0.1f;

        render();
        SDL_Delay(16);
    }

    TTF_CloseFont(g_font_sm);
    TTF_CloseFont(g_font_md);
    TTF_CloseFont(g_font_lg);
    TTF_Quit();
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
