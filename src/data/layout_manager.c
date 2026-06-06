#include "data/layout_manager.h"
#include "widgets/now_playing.h"
#include "widgets/mini_visualizer.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

bool layout_save(WidgetManager *mgr, const char *db_path) {
    sqlite3 *db;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return false;
    }

    const char *create_sql = "CREATE TABLE IF NOT EXISTS widgets ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                             "name TEXT,"
                             "type INTEGER,"
                             "x REAL, y REAL, w REAL, h REAL);";
    sqlite3_exec(db, create_sql, NULL, NULL, NULL);

    sqlite3_exec(db, "DELETE FROM widgets;", NULL, NULL, NULL);

    sqlite3_stmt *stmt;
    const char *insert_sql = "INSERT INTO widgets (name, type, x, y, w, h) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);

    for (int i = 0; i < mgr->count; i++) {
        Widget *w = &mgr->widgets[i];
        if (!w->active) continue;

        sqlite3_bind_text(stmt, 1, w->name, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, w->type);
        sqlite3_bind_double(stmt, 3, w->target_x);
        sqlite3_bind_double(stmt, 4, w->target_y);
        sqlite3_bind_double(stmt, 5, w->target_w);
        sqlite3_bind_double(stmt, 6, w->target_h);

        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return true;
}

bool layout_load(WidgetManager *mgr, const char *db_path, void *ren) {
    sqlite3 *db;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return false;
    }

    const char *query = "SELECT name, type, x, y, w, h FROM widgets;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }

    mgr->count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int type = sqlite3_column_int(stmt, 1);
        float x = sqlite3_column_double(stmt, 2);
        float y = sqlite3_column_double(stmt, 3);
        float w = sqlite3_column_double(stmt, 4);
        float h = sqlite3_column_double(stmt, 5);

        int pre_count = mgr->count;

        if (type == WIDGET_NOW_PLAYING) {
            now_playing_register(mgr, (SDL_Renderer*)ren);
        } else if (type == WIDGET_MINI_VISUALIZER) {
            mini_visualizer_register(mgr, (SDL_Renderer*)ren);
        }

        if (mgr->count > pre_count) {
            Widget *new_w = &mgr->widgets[mgr->count - 1];
            new_w->type = type;
            new_w->target_x = new_w->x = x;
            new_w->target_y = new_w->y = y;
            new_w->target_w = new_w->w = w;
            new_w->target_h = new_w->h = h;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return true;
}
