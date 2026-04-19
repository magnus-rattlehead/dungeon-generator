#include "dungeon.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#define NUM_DIRS    4
#define CHUNK_SIZE  32
#define MAP_BUCKETS 512

/* Direction indices: 0=N(+y), 1=E(+x), 2=S(-y), 3=W(-x) */
static const int DIR_DX[NUM_DIRS] = {  0, +1,  0, -1 };
static const int DIR_DY[NUM_DIRS] = { +1,  0, -1,  0 };
static const char DIR_CHAR[NUM_DIRS] = { 'N', 'E', 'S', 'W' };

/* N↔S = indices 0,2; E↔W = indices 1,3. XOR with 2 flips within each pair. */
#define OPPOSITE(d) ((d) ^ 2)

/* ── PRNG ─────────────────────────────────────────────────────────── */

typedef struct { uint64_t s; } RNG;

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static RNG rng_make(uint64_t seed) {
    /* splitmix64 avoids the xorshift64 fixed-point at zero */
    RNG r; r.s = splitmix64(&seed); return r;
}

static uint64_t rng_next(RNG *r) {
    r->s ^= r->s << 13;
    r->s ^= r->s >> 7;
    r->s ^= r->s << 17;
    return r->s;
}

/* Rejection sampling to avoid modulo bias. */
static uint32_t rng_range(RNG *r, uint32_t n) {
    if (n <= 1) return 0;
    uint64_t threshold = (UINT64_C(1) << 32) - ((UINT64_C(1) << 32) % n);
    uint64_t v;
    do { v = rng_next(r) & UINT64_C(0xFFFFFFFF); } while (v >= threshold);
    return (uint32_t)(v % n);
}

static float rng_float(RNG *r) {
    return (float)(rng_next(r) >> 11) / (float)(UINT64_C(1) << 53);
}

static void rng_shuffle4(RNG *r, int a[NUM_DIRS]) {
    for (int i = NUM_DIRS - 1; i > 0; i--) {
        int j = (int)rng_range(r, (uint32_t)(i + 1));
        int t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

/* ── Spatial helpers ──────────────────────────────────────────────── */

/* C's integer division truncates toward zero; floor_div corrects that for
   negative world coordinates (e.g. floor_div(-1,32) = -1, not 0). */
static int floor_div(int a, int b) {
    return a / b - (a % b != 0 && a < 0 ? 1 : 0);
}

static int chunk_of(int world) { return floor_div(world, CHUNK_SIZE); }
static int local_of(int world) { return world - chunk_of(world) * CHUNK_SIZE; }

/* ── Chunk bitmap (compressed form) ──────────────────────────────── */

#define BITMAP_BYTES (CHUNK_SIZE * CHUNK_SIZE / 8)  /* 128 bytes per band */

/* Fixed-size adjacency bitmap for one chunk. Only N and E edge bands are
   stored — S/W edges are symmetric and reconstructed at render time from
   neighboring rooms' N/E bits, matching how place_room draws corridors. */
typedef struct {
    uint8_t presence[BITMAP_BYTES];
    uint8_t edges_n [BITMAP_BYTES];
    uint8_t edges_e [BITMAP_BYTES];
} ChunkBitmap;  /* 384 bytes */

#define BIT_IDX(lx, ly)     ((ly) * CHUNK_SIZE + (lx))
#define BM_GET(arr, lx, ly) (((arr)[BIT_IDX((lx),(ly))/8] >> (BIT_IDX((lx),(ly))%8)) & 1)
#define BM_SET(arr, lx, ly) ((arr)[BIT_IDX((lx),(ly))/8] |= (uint8_t)(1u << (BIT_IDX((lx),(ly))%8)))

/* ── Data types ───────────────────────────────────────────────────── */

typedef struct Room {
    int x, y;
    struct Room *edges[NUM_DIRS];
} Room;

typedef struct Chunk {
    int  cx, cy;
    bool is_compressed;
    Room *rooms[CHUNK_SIZE][CHUNK_SIZE];  /* [local_y][local_x]; live form */
    uint8_t *zbuf;      /* zlib-deflated ChunkBitmap; NULL when live */
    uLong    zbuf_size;
    struct Chunk *next; /* intrusive hash-chain */
} Chunk;

typedef struct { Chunk *buckets[MAP_BUCKETS]; } ChunkMap;

typedef struct QNode { Room *r; struct QNode *next; } QNode;
typedef struct { QNode *head, *tail; } Queue;

struct Dungeon {
    DungeonConfig cfg;
    ChunkMap      cmap;
    int           room_count;
    int           min_x, min_y, max_x, max_y;
    bool          bounds_valid;
};

/* ── ChunkMap ─────────────────────────────────────────────────────── */

static uint32_t chunk_hash(int cx, int cy) {
    uint32_t h = (uint32_t)cx * UINT32_C(2654435761)
               ^ (uint32_t)cy * UINT32_C(2246822519);
    return (h ^ (h >> 16)) & (MAP_BUCKETS - 1);
}

static Chunk *cmap_find(const ChunkMap *m, int cx, int cy) {
    uint32_t h = chunk_hash(cx, cy);
    for (Chunk *c = m->buckets[h]; c; c = c->next)
        if (c->cx == cx && c->cy == cy) return c;
    return NULL;
}

static Chunk *cmap_get_or_create(ChunkMap *m, int cx, int cy) {
    Chunk *c = cmap_find(m, cx, cy);
    if (c) return c;
    c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->cx = cx; c->cy = cy;
    uint32_t h = chunk_hash(cx, cy);
    c->next = m->buckets[h];
    m->buckets[h] = c;
    return c;
}

static void cmap_free_all(ChunkMap *m) {
    for (int b = 0; b < MAP_BUCKETS; b++) {
        Chunk *c = m->buckets[b];
        while (c) {
            Chunk *nxt = c->next;
            if (c->is_compressed) {
                free(c->zbuf);
            } else {
                for (int y = 0; y < CHUNK_SIZE; y++)
                    for (int x = 0; x < CHUNK_SIZE; x++)
                        free(c->rooms[y][x]);
            }
            free(c);
            c = nxt;
        }
    }
}

/* ── Room helpers ─────────────────────────────────────────────────── */

/* Returns NULL for compressed chunks — BFS never traverses into them. */
static Room *room_at(const ChunkMap *m, int x, int y) {
    Chunk *c = cmap_find(m, chunk_of(x), chunk_of(y));
    if (!c || c->is_compressed) return NULL;
    return c->rooms[local_of(y)][local_of(x)];
}

static Room *room_create(ChunkMap *m, int x, int y) {
    Chunk *c = cmap_get_or_create(m, chunk_of(x), chunk_of(y));
    if (!c) return NULL;
    Room *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->x = x; r->y = y;
    c->rooms[local_of(y)][local_of(x)] = r;
    return r;
}

static void room_connect(Room *a, int dir, Room *b) {
    a->edges[dir] = b;
    b->edges[OPPOSITE(dir)] = a;
}

static int room_edge_count(const Room *r) {
    int n = 0;
    for (int i = 0; i < NUM_DIRS; i++)
        if (r->edges[i]) n++;
    return n;
}

/* ── BFS queue ────────────────────────────────────────────────────── */

static bool q_push(Queue *q, Room *r) {
    QNode *n = malloc(sizeof(*n));
    if (!n) return false;
    n->r = r; n->next = NULL;
    if (q->tail) q->tail->next = n; else q->head = n;
    q->tail = n;
    return true;
}

static Room *q_pop(Queue *q) {
    if (!q->head) return NULL;
    QNode *n = q->head;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    Room *r = n->r;
    free(n);
    return r;
}

static void q_drain(Queue *q) { while (q->head) q_pop(q); }

/* ── BFS generation ───────────────────────────────────────────────── */

static void update_bounds(Dungeon *d, int x, int y) {
    if (!d->bounds_valid) {
        d->min_x = d->max_x = x;
        d->min_y = d->max_y = y;
        d->bounds_valid = true;
        return;
    }
    if (x < d->min_x) d->min_x = x;
    if (x > d->max_x) d->max_x = x;
    if (y < d->min_y) d->min_y = y;
    if (y > d->max_y) d->max_y = y;
}

/* Generate up to `target` rooms via BFS from `start`, clamped to
   [bx0,bx1]×[by0,by1]. Use INT_MIN/INT_MAX for unbounded generation. */
static bool expand_bfs(Dungeon *d, Room *start, int target,
                        RNG *rng, int bx0, int by0, int bx1, int by1) {
    Queue q = {0};
    if (!q_push(&q, start)) return false;

    while (d->room_count < target && q.head) {
        Room *cur = q_pop(&q);

        int dirs[NUM_DIRS] = {0, 1, 2, 3};
        rng_shuffle4(rng, dirs);

        for (int i = 0; i < NUM_DIRS && d->room_count < target; i++) {
            int di = dirs[i];
            if (cur->edges[di]) continue;

            int nx = cur->x + DIR_DX[di];
            int ny = cur->y + DIR_DY[di];

            Room *existing = room_at(&d->cmap, nx, ny);
            if (existing) {
                if (rng_float(rng) < d->cfg.loop_factor)
                    room_connect(cur, di, existing);
                continue;
            }

            if (nx < bx0 || nx > bx1 || ny < by0 || ny > by1) continue;

            /* Allow the first branch from any room unconditionally so the
               dungeon stays connected; gate further branches on branch_factor. */
            if (room_edge_count(cur) >= 2
                    && rng_float(rng) >= d->cfg.branch_factor)
                continue;

            Room *nr = room_create(&d->cmap, nx, ny);
            if (!nr) { q_drain(&q); return false; }

            room_connect(cur, di, nr);
            d->room_count++;
            update_bounds(d, nx, ny);

            if (!q_push(&q, nr)) { q_drain(&q); return false; }
        }
    }

    /* Frontier drain in unbounded mode is unexpected (bounded chunks always
       partially fill — that's normal and not warned about). */
    if (d->room_count < target && !q.head && bx0 == INT_MIN)
        fprintf(stderr,
            "Warning: BFS frontier exhausted at %d/%d rooms "
            "(try a higher branch_factor)\n",
            d->room_count, target);

    q_drain(&q);
    return true;
}

/* ── Public API ───────────────────────────────────────────────────── */

DungeonConfig dungeon_default_config(void) {
    return (DungeonConfig){
        .seed          = 0,
        .num_rooms     = 64,
        .branch_factor = 0.45f,
        .loop_factor   = 0.08f,
    };
}

Dungeon *dungeon_create(DungeonConfig cfg) {
    if (!cfg.seed)
        cfg.seed = (uint64_t)time(NULL) ^ ((uint64_t)(uintptr_t)&cfg >> 4);

    fprintf(stderr, "Seed: %llu\n", (unsigned long long)cfg.seed);

    Dungeon *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->cfg = cfg;

    if (cfg.num_rooms == 0) return d;  /* infinite mode: caller drives via dungeon_extend */

    Room *origin = room_create(&d->cmap, 0, 0);
    if (!origin) { dungeon_destroy(d); return NULL; }
    d->room_count = 1;
    update_bounds(d, 0, 0);

    RNG rng = rng_make(cfg.seed);
    if (!expand_bfs(d, origin, cfg.num_rooms, &rng,
                    INT_MIN, INT_MIN, INT_MAX, INT_MAX)) {
        dungeon_destroy(d);
        return NULL;
    }

    return d;
}

bool dungeon_extend(Dungeon *d, int cx, int cy) {
    if (cmap_find(&d->cmap, cx, cy)) return true;

    /* Deterministic per-chunk seed derived from global seed + coordinates. */
    uint64_t cseed = d->cfg.seed
        ^ ((uint64_t)(uint32_t)cx * UINT64_C(0x9e3779b97f4a7c15))
        ^ ((uint64_t)(uint32_t)cy * UINT64_C(0x6c62272e07bb0142));
    RNG rng = rng_make(cseed);

    int sx = cx * CHUNK_SIZE + CHUNK_SIZE / 2;
    int sy = cy * CHUNK_SIZE + CHUNK_SIZE / 2;

    /* An adjacent chunk may have already placed a room at the center. */
    Room *start = room_at(&d->cmap, sx, sy);
    if (!start) {
        start = room_create(&d->cmap, sx, sy);
        if (!start) return false;
        d->room_count++;
        update_bounds(d, sx, sy);
    }

    int target = d->room_count + CHUNK_SIZE * CHUNK_SIZE;
    int bx0 = cx * CHUNK_SIZE,      by0 = cy * CHUNK_SIZE;
    int bx1 = bx0 + CHUNK_SIZE - 1, by1 = by0 + CHUNK_SIZE - 1;

    return expand_bfs(d, start, target, &rng, bx0, by0, bx1, by1);
}

bool dungeon_extend_viewport(Dungeon *d, int x0, int y0, int x1, int y1) {
    int cx0 = chunk_of(x0), cy0 = chunk_of(y0);
    int cx1 = chunk_of(x1), cy1 = chunk_of(y1);
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++)
            if (!dungeon_extend(d, cx, cy)) return false;
    return true;
}

int dungeon_compress_distant(Dungeon *d, int x0, int y0, int x1, int y1) {
    int freed = 0;
    for (int b = 0; b < MAP_BUCKETS; b++) {
        for (Chunk *c = d->cmap.buckets[b]; c; c = c->next) {
            if (c->is_compressed) continue;

            int wx0 = c->cx * CHUNK_SIZE,      wy0 = c->cy * CHUNK_SIZE;
            int wx1 = wx0 + CHUNK_SIZE - 1,    wy1 = wy0 + CHUNK_SIZE - 1;

            if (!(wx1 < x0 || wx0 > x1 || wy1 < y0 || wy0 > y1)) continue;

            ChunkBitmap bm;
            memset(&bm, 0, sizeof(bm));

            for (int y = 0; y < CHUNK_SIZE; y++) {
                for (int x = 0; x < CHUNK_SIZE; x++) {
                    Room *r = c->rooms[y][x];
                    if (!r) continue;
                    BM_SET(bm.presence, x, y);
                    if (r->edges[0]) BM_SET(bm.edges_n, x, y);
                    if (r->edges[1]) BM_SET(bm.edges_e, x, y);
                    free(r);
                    c->rooms[y][x] = NULL;
                    freed++;
                }
            }

            uLong dest_len = compressBound(sizeof(bm));
            uint8_t *dest = malloc(dest_len);
            if (!dest) continue;  /* rooms already freed; mark compressed below */
            if (compress2(dest, &dest_len,
                          (const Bytef *)&bm, (uLong)sizeof(bm),
                          Z_DEFAULT_COMPRESSION) != Z_OK) {
                free(dest); continue;
            }
            c->zbuf      = dest;
            c->zbuf_size = dest_len;
            c->is_compressed = true;
        }
    }
    return freed;
}

void dungeon_print_rooms(const Dungeon *d, int max) {
    int printed = 0;
    for (int b = 0; b < MAP_BUCKETS; b++) {
        for (Chunk *c = d->cmap.buckets[b]; c; c = c->next) {
            if (c->is_compressed) {
                ChunkBitmap bm;
                uLong bm_len = (uLong)sizeof(bm);
                if (uncompress((Bytef *)&bm, &bm_len,
                               c->zbuf, c->zbuf_size) != Z_OK) continue;
                for (int ly = 0; ly < CHUNK_SIZE; ly++) {
                    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                        if (!BM_GET(bm.presence, lx, ly)) continue;
                        if (max > 0 && printed >= max) goto done;
                        int wx = c->cx * CHUNK_SIZE + lx;
                        int wy = c->cy * CHUNK_SIZE + ly;
                        printf("Room (%d,%d) edges:", wx, wy);
                        if (BM_GET(bm.edges_n, lx, ly)) printf(" N");
                        if (BM_GET(bm.edges_e, lx, ly)) printf(" E");
                        printf(" (compressed: N/E only)\n");
                        printed++;
                    }
                }
            } else {
                for (int y = 0; y < CHUNK_SIZE; y++) {
                    for (int x = 0; x < CHUNK_SIZE; x++) {
                        if (max > 0 && printed >= max) goto done;
                        Room *r = c->rooms[y][x];
                        if (!r) continue;
                        printf("Room (%d,%d) edges:", r->x, r->y);
                        for (int di = 0; di < NUM_DIRS; di++)
                            if (r->edges[di]) printf(" %c", DIR_CHAR[di]);
                        printf("\n");
                        printed++;
                    }
                }
            }
        }
    }
done:
    if (max > 0 && printed >= max)
        printf("... (output truncated at %d rooms)\n", max);
}

/* ── Rendering ────────────────────────────────────────────────────── */

/* World Y increases upward; the buffer prints top-to-bottom, so we flip:
     buf_row = gh - 1 - gy
   Only N (|) and E (-) corridor glyphs are drawn per room; S/W corridors
   are produced by the neighboring room's N/E glyph. */
static void place_room(char *buf, int gw, int gh,
                        int x0, int y0, int wx, int wy, uint8_t edge_mask) {
    if (wx < x0 || wy < y0) return;
    int gx = (wx - x0) * 2;
    int gy = (wy - y0) * 2;
    if (gx >= gw || gy >= gh) return;

    int row = gh - 1 - gy;
    buf[row * (gw + 1) + gx] = '#';

    if ((edge_mask & (1 << 0)) && gy + 1 < gh)
        buf[(row - 1) * (gw + 1) + gx] = '|';

    if ((edge_mask & (1 << 1)) && gx + 1 < gw)
        buf[row * (gw + 1) + gx + 1] = '-';
}

char *dungeon_render(const Dungeon *d, int x0, int y0, int x1, int y1) {
    if (x1 < x0 || y1 < y0) return NULL;

    if ((x1 - x0) > DUNGEON_RENDER_MAX_DIM || (y1 - y0) > DUNGEON_RENDER_MAX_DIM) {
        fprintf(stderr,
            "dungeon_render: viewport %dx%d exceeds DUNGEON_RENDER_MAX_DIM (%d). "
            "Use a smaller viewport or #define DUNGEON_RENDER_MAX_DIM before "
            "including dungeon.h.\n",
            x1 - x0, y1 - y0, DUNGEON_RENDER_MAX_DIM);
        return NULL;
    }

    int gw = (x1 - x0) * 2 + 1;
    int gh = (y1 - y0) * 2 + 1;
    size_t row_len = (size_t)gw + 1;
    size_t buf_sz  = (size_t)gh * row_len + 1;

    char *buf = malloc(buf_sz);
    if (!buf) return NULL;

    memset(buf, ' ', buf_sz);
    for (int row = 0; row < gh; row++)
        buf[row * row_len + gw] = '\n';
    buf[buf_sz - 1] = '\0';

    for (int b = 0; b < MAP_BUCKETS; b++) {
        for (Chunk *c = d->cmap.buckets[b]; c; c = c->next) {
            if (c->is_compressed) {
                ChunkBitmap bm;
                uLong bm_len = (uLong)sizeof(bm);
                if (uncompress((Bytef *)&bm, &bm_len,
                               c->zbuf, c->zbuf_size) != Z_OK) continue;
                for (int ly = 0; ly < CHUNK_SIZE; ly++) {
                    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                        if (!BM_GET(bm.presence, lx, ly)) continue;
                        uint8_t em = 0;
                        if (BM_GET(bm.edges_n, lx, ly)) em |= (uint8_t)(1u << 0);
                        if (BM_GET(bm.edges_e, lx, ly)) em |= (uint8_t)(1u << 1);
                        place_room(buf, gw, gh, x0, y0,
                                   c->cx * CHUNK_SIZE + lx,
                                   c->cy * CHUNK_SIZE + ly, em);
                    }
                }
            } else {
                for (int y = 0; y < CHUNK_SIZE; y++) {
                    for (int x = 0; x < CHUNK_SIZE; x++) {
                        Room *r = c->rooms[y][x];
                        if (!r) continue;
                        uint8_t em = 0;
                        for (int di = 0; di < NUM_DIRS; di++)
                            if (r->edges[di]) em |= (uint8_t)(1 << di);
                        place_room(buf, gw, gh, x0, y0, r->x, r->y, em);
                    }
                }
            }
        }
    }

    return buf;
}

char *dungeon_render_full(const Dungeon *d) {
    int x0, y0, x1, y1;
    if (!dungeon_bounds(d, &x0, &y0, &x1, &y1)) {
        char *s = malloc(2);
        if (s) { s[0] = '\n'; s[1] = '\0'; }
        return s;
    }
    return dungeon_render(d, x0, y0, x1, y1);
}

void dungeon_destroy(Dungeon *d) {
    if (!d) return;
    cmap_free_all(&d->cmap);
    free(d);
}

uint64_t dungeon_seed(const Dungeon *d)       { return d->cfg.seed; }
int      dungeon_room_count(const Dungeon *d) { return d->room_count; }

bool dungeon_bounds(const Dungeon *d, int *x0, int *y0, int *x1, int *y1) {
    if (!d->bounds_valid) return false;
    *x0 = d->min_x; *y0 = d->min_y;
    *x1 = d->max_x; *y1 = d->max_y;
    return true;
}
