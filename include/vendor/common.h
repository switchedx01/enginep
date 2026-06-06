#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* P10 Rule 8: Limited Preprocessor. Only includes and simple macros. */
/* P10 Rule 5: Assertion Density. Recovery action must be taken. */

/* Debugging function prototype (implemented in hub_context.c) */
void tst_debugging(const char *format, const char *file, int line,
                   const char *expr);

/* P10 Assertion Macro */
static inline bool _c_assert_impl(bool cond, const char* file, int line, const char* expr) {
    if (!cond) {
        tst_debugging("Assertion failed", file, line, expr);
        return false;
    }
    return true;
}

#define c_assert(e) _c_assert_impl((e), __FILE__, __LINE__, #e)

/* Standard Return Codes */
typedef enum {
  RESULT_SUCCESS = 0,
  RESULT_ERROR_GENERIC = -1,
  RESULT_ERROR_NULL_POINTER = -2,
  RESULT_ERROR_BUFFER_OVERFLOW = -3,
  RESULT_ERROR_INVALID_PARAMETER = -4,
  RESULT_ERROR_FILE_IO = -5,
  RESULT_ERROR_NOT_IMPLEMENTED = -6,
  RESULT_ERROR_OUT_OF_MEMORY = -7
} Result;

/* P10 Rule 7: Strict Interface Validation Macros */
#define VALIDATE_PTR_OR_RETURN(ptr, ret)                                       \
  if (!(ptr)) {                                                                \
    c_assert(false);                                                           \
    return (ret);                                                              \
  }

#define VALIDATE_RESULT(res)                                                   \
  if ((res) != RESULT_SUCCESS) {                                               \
    c_assert(false);                                                           \
    return (res);                                                              \
  }

/* Max Constraints for Static Allocation (P10 Rule 3) */
#define MAX_PATH_LENGTH 1024
#define MAX_LOG_LINE 1024
#define MAX_SONG_TITLE 256

/* Hub-specific Constants */
#define HUB_WINDOW_WIDTH 900
#define HUB_WINDOW_HEIGHT 600
#define HUB_SIDEBAR_WIDTH 250
#define HUB_MAX_SEARCH_RESULTS 5
#define HUB_DB_PATH                                                            \
  "/mnt/mass-storage/Archive/Documents/dev/Harmony_Retooled/harmony_v2.db"
#define HUB_FONT_PATH "assets/fonts/Roboto-Regular.ttf"

#endif /* COMMON_H */
