#ifndef CORE_MEMORY_H
#define CORE_MEMORY_H

#include "vendor/common.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* 
 * Professional Ruleset: Structural Excellence
 * SRP: This module is strictly responsible for bump-allocator logic.
 * Composition over Inheritance: Use MemoryArena struct to avoid global state.
 */

typedef struct {
    uint8_t* buffer;
    size_t capacity;
    size_t offset;
    bool initialized;
} MemoryArena;

/* Naming Standards: Use clear Verbs for functions, Nouns for arguments. */
void initialize_memory_arena(MemoryArena* arena, void* backing_buffer, size_t capacity);
void* allocate_from_arena(MemoryArena* arena, size_t size);
void reset_memory_arena(MemoryArena* arena);

#endif /* CORE_MEMORY_H */
