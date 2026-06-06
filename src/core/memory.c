#include "core/memory.h"
#include "vendor/common.h"

/* 
 * Professional Ruleset: Structural Excellence
 * Pure Functions: Keep alignment math isolated and deterministic without side-effects.
 * DRY: Single source of truth for alignment.
 */
static size_t calculate_aligned_size(size_t size, size_t alignment) {
    /* KISS: Simple bitwise alignment (requires alignment to be power of 2) */
    return (size + (alignment - 1)) & ~(alignment - 1);
}

void initialize_memory_arena(MemoryArena* arena, void* backing_buffer, size_t capacity) {
    if (!arena || !backing_buffer) {
        c_assert(false);
        return;
    }
    
    arena->buffer = (uint8_t*)backing_buffer;
    arena->capacity = capacity;
    arena->offset = 0;
    arena->initialized = true;
}

void* allocate_from_arena(MemoryArena* arena, size_t size) {
    VALIDATE_PTR_OR_RETURN(arena, NULL);
    VALIDATE_PTR_OR_RETURN(arena->initialized, NULL);
    VALIDATE_PTR_OR_RETURN(size > 0, NULL);
    
    /* Anti-pattern fix: No magic numbers. Use named constant for alignment. */
    const size_t DEFAULT_ALIGNMENT = 8;
    size_t aligned_size = calculate_aligned_size(size, DEFAULT_ALIGNMENT);
    
    if (arena->offset + aligned_size > arena->capacity) {
        c_assert(false); /* P10: Assert on out of memory instead of failing silently */
        return NULL;
    }
    
    void* ptr = arena->buffer + arena->offset;
    arena->offset += aligned_size;
    
    return ptr;
}

void reset_memory_arena(MemoryArena* arena) {
    if (!arena) {
        c_assert(false);
        return;
    }
    arena->offset = 0;
}
