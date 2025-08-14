// arena.h
#pragma once
#include <stddef.h>
#include <stdint.h>

void arena_init(size_t capacity);
void arena_shutdown(void);
void* arena_alloc(size_t size);
void arena_reset(void);
size_t arena_used(void);

// Convenience macro for typed allocation
#define ARENA_ALLOC(type) ((type*)arena_alloc(sizeof(type)))
#define ARENA_ALLOC_ARRAY(type, count) ((type*)arena_alloc(sizeof(type) * (count)))
