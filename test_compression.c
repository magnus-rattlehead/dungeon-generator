/**
 * test_compression.c — Unit tests for chunk compression.
 *
 * Core invariant: render(d, viewport) is identical before and after
 * dungeon_compress_distant() for any viewport that doesn't intersect
 * the compressed area.
 */

#include "dungeon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "    ASSERT failed [%s:%d]: %s\n",        \
                    __FILE__, __LINE__, (msg));                        \
            return false;                                              \
        }                                                              \
    } while (0)

static void run(const char *name, bool (*fn)(void)) {
    fprintf(stderr, "  %-52s", name);
    if (fn()) { fprintf(stderr, "PASS\n"); g_passed++; }
    else       { fprintf(stderr, "FAIL\n"); g_failed++; }
}

static char *render_viewport(Dungeon *d, int x0, int y0, int x1, int y1) {
    return dungeon_render(d, x0, y0, x1, y1);
}

static char *render_full(Dungeon *d) {
    return dungeon_render_full(d);
}

static Dungeon *make_dungeon(uint64_t seed, int rooms) {
    DungeonConfig cfg = dungeon_default_config();
    cfg.seed      = seed;
    cfg.num_rooms = rooms;
    return dungeon_create(cfg);
}

/* ── Tests ──────────────────────────────────────────────────────── */

static bool test_room_count_preserved(void) {
    Dungeon *d = make_dungeon(1, 100);
    ASSERT(d, "dungeon_create returned NULL");

    int before = dungeon_room_count(d);
    ASSERT(before > 0, "expected non-zero room count");

    dungeon_compress_distant(d, 999999, 999999, 999999, 999999);

    ASSERT(dungeon_room_count(d) == before, "room_count changed after compression");

    dungeon_destroy(d);
    return true;
}

static bool test_full_render_identical_after_full_compress(void) {
    Dungeon *d = make_dungeon(2, 80);
    ASSERT(d, "dungeon_create returned NULL");

    char *before = render_full(d);
    ASSERT(before, "render_full returned NULL before compression");

    dungeon_compress_distant(d, 999999, 999999, 999999, 999999);

    char *after = render_full(d);
    ASSERT(after, "render_full returned NULL after compression");
    ASSERT(strcmp(before, after) == 0, "render output changed after compressing all chunks");

    free(before); free(after);
    dungeon_destroy(d);
    return true;
}

static bool test_viewport_render_identical_after_partial_compress(void) {
    Dungeon *d = make_dungeon(3, 120);
    ASSERT(d, "dungeon_create returned NULL");

    int x0 = -5, y0 = -5, x1 = 5, y1 = 5;
    char *before = render_viewport(d, x0, y0, x1, y1);
    ASSERT(before, "render before compression returned NULL");

    dungeon_compress_distant(d, x0, y0, x1, y1);

    char *after = render_viewport(d, x0, y0, x1, y1);
    ASSERT(after, "render after compression returned NULL");
    ASSERT(strcmp(before, after) == 0, "viewport render changed after partial compression");

    free(before); free(after);
    dungeon_destroy(d);
    return true;
}

static bool test_full_render_identical_after_partial_compress(void) {
    Dungeon *d = make_dungeon(4, 100);
    ASSERT(d, "dungeon_create returned NULL");

    char *before = render_full(d);
    ASSERT(before, "render_full before compression returned NULL");

    dungeon_compress_distant(d, -3, -3, 3, 3);

    char *after = render_full(d);
    ASSERT(after, "render_full after partial compression returned NULL");
    ASSERT(strcmp(before, after) == 0, "full render changed after partial compression");

    free(before); free(after);
    dungeon_destroy(d);
    return true;
}

static bool test_freed_count_equals_total_rooms(void) {
    Dungeon *d = make_dungeon(5, 64);
    ASSERT(d, "dungeon_create returned NULL");

    int total = dungeon_room_count(d);
    int freed = dungeon_compress_distant(d, 999999, 999999, 999999, 999999);
    ASSERT(freed == total, "freed count did not equal total when compressing all");

    dungeon_destroy(d);
    return true;
}

static bool test_freed_count_respects_viewport(void) {
    Dungeon *d = make_dungeon(6, 80);
    ASSERT(d, "dungeon_create returned NULL");

    int total = dungeon_room_count(d);
    int freed = dungeon_compress_distant(d, -2, -2, 2, 2);

    ASSERT(freed < total, "freed count should be less than total (viewport kept some alive)");
    ASSERT(freed >= 0,    "freed count must not be negative");
    ASSERT(total - freed > 0, "no rooms survived compression — viewport had no effect");

    dungeon_destroy(d);
    return true;
}

static bool test_idempotent(void) {
    Dungeon *d = make_dungeon(7, 60);
    ASSERT(d, "dungeon_create returned NULL");

    int freed1 = dungeon_compress_distant(d, 999999, 999999, 999999, 999999);
    ASSERT(freed1 > 0, "first compress freed nothing — dungeon may be empty");

    int freed2 = dungeon_compress_distant(d, 999999, 999999, 999999, 999999);
    ASSERT(freed2 == 0, "second compress freed rooms that were already compressed");

    char *map = render_full(d);
    ASSERT(map, "render_full returned NULL after double compression");
    free(map);

    dungeon_destroy(d);
    return true;
}

static bool test_compress_empty_dungeon(void) {
    DungeonConfig cfg = dungeon_default_config();
    cfg.seed      = 8;
    cfg.num_rooms = 0;
    Dungeon *d = dungeon_create(cfg);
    ASSERT(d, "dungeon_create returned NULL");
    ASSERT(dungeon_room_count(d) == 0, "expected 0 rooms in freshly created infinite dungeon");
    ASSERT(dungeon_compress_distant(d, 0, 0, 31, 31) == 0,
           "compress on empty dungeon should free 0");
    dungeon_destroy(d);
    return true;
}

static bool test_negative_coord_rooms(void) {
    DungeonConfig cfg = dungeon_default_config();
    cfg.seed          = 9;
    cfg.num_rooms     = 100;
    cfg.branch_factor = 0.3f;
    Dungeon *d = dungeon_create(cfg);
    ASSERT(d, "dungeon_create returned NULL");
    ASSERT(dungeon_bounds(d, &(int){0}, &(int){0}, &(int){0}, &(int){0}),
           "dungeon_bounds returned false for non-empty dungeon");

    char *before = render_full(d);
    ASSERT(before, "render_full before compression returned NULL");

    dungeon_compress_distant(d, 999999, 999999, 999999, 999999);

    char *after = render_full(d);
    ASSERT(after, "render_full after compression returned NULL");
    ASSERT(strcmp(before, after) == 0, "render changed after compression (negative-coord dungeon)");

    free(before); free(after);
    dungeon_destroy(d);
    return true;
}

static bool test_large_dungeon_partial_compress(void) {
    Dungeon *d = make_dungeon(10, 1000);
    ASSERT(d, "dungeon_create returned NULL");

    int total = dungeon_room_count(d);
    ASSERT(total > 0, "expected rooms in large dungeon");

    /* Keep only chunk (0,0) = world [0,31]×[0,31]. A 1000-room dungeon always
       spreads beyond that, so freed > 0. Using a multi-quadrant viewport like
       (-3,-3,3,3) would keep all four origin-adjacent chunks alive and might
       contain the entire dungeon — hence the single-chunk viewport here. */
    int vx0 = 0, vy0 = 0, vx1 = 0, vy1 = 0;
    char *before = render_viewport(d, vx0, vy0, vx1, vy1);
    ASSERT(before, "render before compression returned NULL");

    int freed = dungeon_compress_distant(d, vx0, vy0, vx1, vy1);
    ASSERT(freed > 0,     "expected rooms to be freed outside small viewport");
    ASSERT(freed < total, "freed count must be less than total (viewport is alive)");

    char *after = render_viewport(d, vx0, vy0, vx1, vy1);
    ASSERT(after, "render after compression returned NULL");
    ASSERT(strcmp(before, after) == 0,
           "viewport render changed after partial compression of large dungeon");

    free(before); free(after);
    dungeon_destroy(d);
    return true;
}

static bool test_infinite_mode_compress_then_extend(void) {
    DungeonConfig cfg = dungeon_default_config();
    cfg.seed      = 11;
    cfg.num_rooms = 0;
    Dungeon *d = dungeon_create(cfg);
    ASSERT(d, "dungeon_create returned NULL");

    ASSERT(dungeon_extend(d, 0, 0), "dungeon_extend(0,0) failed");
    ASSERT(dungeon_room_count(d) > 0, "no rooms after extending chunk (0,0)");

    int va_x0 = 0, va_y0 = 0, va_x1 = 31, va_y1 = 31;
    char *render_a1 = render_viewport(d, va_x0, va_y0, va_x1, va_y1);
    ASSERT(render_a1, "render of chunk (0,0) returned NULL");

    dungeon_compress_distant(d, va_x0, va_y0, va_x1, va_y1);

    ASSERT(dungeon_extend(d, 1, 0), "dungeon_extend(1,0) failed");

    char *render_a2 = render_viewport(d, va_x0, va_y0, va_x1, va_y1);
    ASSERT(render_a2, "render of viewport A after extend(1,0) returned NULL");
    ASSERT(strcmp(render_a1, render_a2) == 0,
           "viewport A render changed after extending a different chunk");

    char *combined = render_viewport(d, 0, 0, 63, 31);
    ASSERT(combined, "combined viewport render returned NULL");
    ASSERT(strchr(combined, '#') != NULL, "combined viewport render has no rooms");

    free(render_a1); free(render_a2); free(combined);
    dungeon_destroy(d);
    return true;
}

static bool test_render_size_guard(void) {
    Dungeon *d = make_dungeon(12, 50);
    ASSERT(d, "dungeon_create returned NULL");

    free(dungeon_render(d, 0, 0, DUNGEON_RENDER_MAX_DIM, DUNGEON_RENDER_MAX_DIM));

    char *over = dungeon_render(d, 0, 0,
                                DUNGEON_RENDER_MAX_DIM + 1,
                                DUNGEON_RENDER_MAX_DIM + 1);
    ASSERT(over == NULL, "dungeon_render should return NULL for oversized viewport");

    dungeon_destroy(d);
    return true;
}

static bool test_compress_with_exact_bounding_box_frees_nothing(void) {
    Dungeon *d = make_dungeon(13, 80);
    ASSERT(d, "dungeon_create returned NULL");

    int x0, y0, x1, y1;
    ASSERT(dungeon_bounds(d, &x0, &y0, &x1, &y1), "dungeon_bounds returned false");
    ASSERT(dungeon_compress_distant(d, x0, y0, x1, y1) == 0,
           "compressing with exact bounding box should free 0 rooms");

    dungeon_destroy(d);
    return true;
}

/* Dense dungeon (branch=0.9) exercises the zlib path under near-full chunk
   fill — the primary motivation for the ChunkBitmap format. */
static bool test_zlib_render_identical_dense(void) {
    DungeonConfig cfg = dungeon_default_config();
    cfg.seed          = 99;
    cfg.num_rooms     = 1000;
    cfg.branch_factor = 0.9f;
    Dungeon *d = dungeon_create(cfg);
    ASSERT(d, "dungeon_create returned NULL");

    char *before = render_full(d);
    ASSERT(before, "render_full before compression returned NULL");

    dungeon_compress_distant(d, 999999, 999999, 999999, 999999);

    char *after = render_full(d);
    ASSERT(after, "render_full after compression returned NULL");
    ASSERT(strcmp(before, after) == 0, "dense dungeon render changed after zlib compression");

    free(before); free(after);
    dungeon_destroy(d);
    return true;
}

static bool test_zlib_viewport_render_after_full_compress(void) {
    DungeonConfig cfg = dungeon_default_config();
    cfg.seed          = 42;
    cfg.num_rooms     = 500;
    cfg.branch_factor = 0.85f;
    Dungeon *d = dungeon_create(cfg);
    ASSERT(d, "dungeon_create returned NULL");

    int x0, y0, x1, y1;
    ASSERT(dungeon_bounds(d, &x0, &y0, &x1, &y1), "dungeon_bounds failed");

    char *before = render_viewport(d, x0, y0, x0 + 10, y0 + 10);
    ASSERT(before, "render before compression returned NULL");

    int freed = dungeon_compress_distant(d, 999999, 999999, 999999, 999999);
    ASSERT(freed > 0, "expected rooms freed from dense dungeon");

    char *after = render_viewport(d, x0, y0, x0 + 10, y0 + 10);
    ASSERT(after, "render after full compression returned NULL");
    ASSERT(strcmp(before, after) == 0, "viewport render changed after full zlib compression");

    free(before); free(after);
    dungeon_destroy(d);
    return true;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void) {
    fprintf(stderr, "Chunk compression tests\n\n");

    run("room_count preserved after full compress",
        test_room_count_preserved);
    run("full render identical after full compress",
        test_full_render_identical_after_full_compress);
    run("viewport render identical after partial compress",
        test_viewport_render_identical_after_partial_compress);
    run("full render identical after partial compress",
        test_full_render_identical_after_partial_compress);
    run("freed count equals total when compressing all",
        test_freed_count_equals_total_rooms);
    run("freed count respects keep-alive viewport",
        test_freed_count_respects_viewport);
    run("compression is idempotent",
        test_idempotent);
    run("compress empty dungeon (infinite mode, no chunks)",
        test_compress_empty_dungeon);
    run("negative-coord rooms compress and render correctly",
        test_negative_coord_rooms);
    run("large dungeon partial compress preserves viewport render",
        test_large_dungeon_partial_compress);
    run("infinite mode: compress then extend adjacent chunk",
        test_infinite_mode_compress_then_extend);
    run("render size guard rejects oversized viewport",
        test_render_size_guard);
    run("compress with exact bounding box frees nothing",
        test_compress_with_exact_bounding_box_frees_nothing);
    run("zlib: dense dungeon render identical after compress",
        test_zlib_render_identical_dense);
    run("zlib: viewport render correct after full compress",
        test_zlib_viewport_render_after_full_compress);

    fprintf(stderr, "\n%d/%d tests passed.\n",
            g_passed, g_passed + g_failed);
    return g_failed > 0 ? 1 : 0;
}
