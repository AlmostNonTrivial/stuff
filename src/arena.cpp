// arena.c
#include "arena.hpp"
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>

static struct {
  uint8_t *base;
  uint8_t *current;
  size_t capacity;
} g_arena = {0};

void arena_init(size_t capacity) {
  g_arena.base = (uint8_t *)malloc(capacity);
  if (!g_arena.base) {
    fprintf(stderr, "Failed to allocate arena: %zu bytes\n", capacity);
    exit(1);
  }
  g_arena.current = g_arena.base;
  g_arena.capacity = capacity;
}

void arena_shutdown(void) {
  free(g_arena.base);
  g_arena.base = NULL;
  g_arena.current = NULL;
  g_arena.capacity = 0;
}

void *arena_alloc(size_t size) {
  // Align to 16 bytes for simplicity
  //
  // Worth explaining alignment
  size_t align = 16;
  uintptr_t current_addr = (uintptr_t)g_arena.current;
  uintptr_t aligned_addr = (current_addr + align - 1) & ~(align - 1);

  uint8_t *aligned = (uint8_t *)aligned_addr;
  uint8_t *next = aligned + size;

  if (next > g_arena.base + g_arena.capacity) {
    fprintf(stderr, "Arena exhausted: requested %zu, used %zu/%zu\n", size,
            arena_used(), g_arena.capacity);
    return NULL;
  }

  g_arena.current = next;
  return aligned;
}
// [previous data][9 bytes wasted padding][20 bytes of p1][0 padding][8 bytes of
// p2][4 wasted][4 bytes of p3]
//              ^                         ^                ^ ^
//           0x1007                    0x1010           0x1024 0x1030

void arena_reset(void) { g_arena.current = g_arena.base; }

size_t arena_used(void) { return g_arena.current - g_arena.base; }
