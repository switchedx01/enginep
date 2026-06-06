/* =========================================================================
 * search.c — SQLite search queries against Harmony database
 * ========================================================================= */

#include "search.h"
#include <sqlite3.h>
#include <string.h>
#include <stdio.h>

Result perform_search(const char *query, SearchResult *results, int *count) {
  if (!c_assert(query != NULL) || !c_assert(results != NULL) || !c_assert(count != NULL))
    return RESULT_ERROR_NULL_POINTER;

  if (strlen(query) < 2) {
    *count = 0;
    return RESULT_SUCCESS;
  }

  sqlite3 *db;
  if (sqlite3_open(HUB_DB_PATH, &db) != SQLITE_OK)
    return RESULT_ERROR_FILE_IO;

  const char *sql =
      "SELECT t.title, a.name, t.filepath, al.art_filename FROM tracks t "
      "LEFT JOIN artists a ON t.artist_id = a.id "
      "LEFT JOIN albums al ON t.album_id = al.id "
      "WHERE t.title LIKE ? LIMIT ?;";
      
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    sqlite3_close(db);
    return RESULT_ERROR_GENERIC;
  }

  char pattern[MAX_SONG_TITLE + 2];
  snprintf(pattern, sizeof(pattern), "%%%s%%", query);
  sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, HUB_MAX_SEARCH_RESULTS);

  int found_count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && found_count < HUB_MAX_SEARCH_RESULTS) {
    SearchResult *r = &results[found_count];
    
    strncpy(r->title, (const char *)sqlite3_column_text(stmt, 0), MAX_SONG_TITLE - 1);
    r->title[MAX_SONG_TITLE - 1] = '\0';
    
    const char *artist = (const char *)sqlite3_column_text(stmt, 1);
    strncpy(r->artist, artist ? artist : "Unknown", MAX_SONG_TITLE - 1);
    r->artist[MAX_SONG_TITLE - 1] = '\0';
    
    strncpy(r->filepath, (const char *)sqlite3_column_text(stmt, 2), MAX_PATH_LENGTH - 1);
    r->filepath[MAX_PATH_LENGTH - 1] = '\0';
    
    const char *art = (const char *)sqlite3_column_text(stmt, 3);
    strncpy(r->art_path, art ? art : "", MAX_PATH_LENGTH - 1);
    r->art_path[MAX_PATH_LENGTH - 1] = '\0';
    
    found_count++;
  }

  *count = found_count;
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return RESULT_SUCCESS;
}
