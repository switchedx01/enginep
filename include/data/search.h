#ifndef SEARCH_H
#define SEARCH_H

#include "hub_context.h" /* Still needed for SearchResult and Result types */

/* Perform a fuzzy title search against the Harmony database.
   Returns a list of results directly without modifying global state. */
Result perform_search(const char *query, SearchResult *results, int *count);

#endif /* SEARCH_H */
