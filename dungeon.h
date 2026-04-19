/**
 * dungeon.h — Public API for the procedural dungeon generator.
 *
 * BFS generation with a seeded xorshift64 RNG; same seed + config always
 * produces the same dungeon.
 *
 * Basic usage:
 *   DungeonConfig cfg = dungeon_default_config();
 *   cfg.num_rooms = 100;
 *   Dungeon *d = dungeon_create(cfg);
 *   char *map = dungeon_render_full(d);
 *   puts(map); free(map);
 *   dungeon_destroy(d);
 *
 * Infinite / chunked mode:
 *   cfg.num_rooms = 0;
 *   Dungeon *d = dungeon_create(cfg);
 *   dungeon_extend_viewport(d, -63, -63, 63, 63);
 *   char *map = dungeon_render(d, -63, -63, 63, 63);
 *   dungeon_compress_distant(d, -63, -63, 63, 63);
 *
 * dungeon_render() returns NULL for viewports wider or taller than
 * DUNGEON_RENDER_MAX_DIM rooms. Override by defining the macro before
 * including this header.
 */
#ifndef DUNGEON_H
#define DUNGEON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef DUNGEON_RENDER_MAX_DIM
#define DUNGEON_RENDER_MAX_DIM 1000
#endif

typedef struct {
    uint64_t seed;          /**< RNG seed. 0 = time-derived (always printed to stderr). */
    int      num_rooms;     /**< Target room count. 0 = infinite / extend-on-demand.   */
    float    branch_factor; /**< [0,1]: 0 = winding corridor, 1 = dense grid.          */
    float    loop_factor;   /**< [0,1]: probability of connecting to an existing room.  */
} DungeonConfig;

typedef struct Dungeon Dungeon;

DungeonConfig dungeon_default_config(void);

/**
 * Generate a dungeon. Prints the resolved seed to stderr.
 * Returns NULL on allocation failure.
 */
Dungeon *dungeon_create(DungeonConfig cfg);

/**
 * Generate chunk (cx, cy) if not already done. Each chunk uses a
 * deterministic seed derived from the global seed and coordinates, so
 * generation order does not affect the result.
 */
bool dungeon_extend(Dungeon *d, int cx, int cy);

/** Extend all chunks overlapping the world-coordinate viewport [x0,x1]×[y0,y1]. */
bool dungeon_extend_viewport(Dungeon *d, int x0, int y0, int x1, int y1);

/**
 * Compress all chunks outside [x0,x1]×[y0,y1]. Frees live Room structs;
 * compressed chunks remain renderable. Returns the number of rooms freed.
 */
int dungeon_compress_distant(Dungeon *d, int x0, int y0, int x1, int y1);

/** Safe to call with NULL. */
void dungeon_destroy(Dungeon *d);

uint64_t dungeon_seed(const Dungeon *d);
int      dungeon_room_count(const Dungeon *d);
int      dungeon_chunk_count(const Dungeon *d);
size_t   dungeon_compressed_bytes(const Dungeon *d); /**< Sum of zbuf sizes over compressed chunks. */

/** Returns false if the dungeon has no rooms. */
bool dungeon_bounds(const Dungeon *d, int *x0, int *y0, int *x1, int *y1);

/** Print one line per room to stdout. max > 0 caps output; 0 = unlimited. */
void dungeon_print_rooms(const Dungeon *d, int max);

/**
 * Render [x0,x1]×[y0,y1] (world coords, inclusive) to a heap-allocated
 * NUL-terminated string. Returns NULL on allocation failure or oversized viewport.
 */
char *dungeon_render(const Dungeon *d, int x0, int y0, int x1, int y1);

/** Render the full bounding box. */
char *dungeon_render_full(const Dungeon *d);

#endif /* DUNGEON_H */
