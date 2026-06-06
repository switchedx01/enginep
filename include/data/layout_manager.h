#ifndef LAYOUT_MANAGER_H
#define LAYOUT_MANAGER_H

#include "widgets/widget_system.h"
#include <stdbool.h>

bool layout_save(WidgetManager *mgr, const char *db_path);
bool layout_load(WidgetManager *mgr, const char *db_path, void *ren);

#endif /* LAYOUT_MANAGER_H */
