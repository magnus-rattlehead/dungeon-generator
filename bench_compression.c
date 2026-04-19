/**
 * bench_compression.c — Measure zlib compression effectiveness.
 *
 * For each scenario: generate a dungeon, record room/chunk counts, compress
 * all chunks, then report compressed size vs. raw ChunkBitmap (384 bytes × chunks).
 */

#include "dungeon.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const char *label;
    uint64_t    seed;
    int         rooms;
    float       branch;
    float       loop;
} Scenario;

static void run(const Scenario *s) {
    DungeonConfig cfg = dungeon_default_config();
    cfg.seed          = s->seed;
    cfg.num_rooms     = s->rooms;
    cfg.branch_factor = s->branch;
    cfg.loop_factor   = s->loop;

    Dungeon *d = dungeon_create(cfg);
    if (!d) { fprintf(stderr, "dungeon_create failed\n"); return; }

    int    rooms  = dungeon_room_count(d);
    int    chunks = dungeon_chunk_count(d);
    dungeon_compress_distant(d, 999999, 999999, 999999, 999999);
    size_t zbytes  = dungeon_compressed_bytes(d);
    size_t raw     = (size_t)chunks * 384;

    printf("%-38s  rooms=%7d  chunks=%4d  "
           "compressed=%6zu B  raw=%7zu B  ratio=%.2fx\n",
           s->label, rooms, chunks, zbytes, raw,
           (double)raw / (double)zbytes);

    dungeon_destroy(d);
}

int main(void) {
    printf("%-38s  %-14s  %-11s  %-18s  %-14s  ratio\n",
           "scenario", "rooms", "chunks", "compressed", "raw (384B/chunk)");
    printf("%s\n",
        "------------------------------------------------------------"
        "----------------------------------------------");

    Scenario scenarios[] = {
        { "sparse  n=100  b=0.2 l=0.0",   42, 100,   0.2f, 0.0f },
        { "default n=100  b=0.45 l=0.08",  42, 100,  0.45f, 0.08f },
        { "dense   n=100  b=0.9 l=0.2",    42, 100,   0.9f, 0.2f },
        { "sparse  n=1000 b=0.2 l=0.0",    99, 1000,  0.2f, 0.0f },
        { "default n=1000 b=0.45 l=0.08",  99, 1000, 0.45f, 0.08f },
        { "dense   n=1000 b=0.9 l=0.2",    99, 1000,  0.9f, 0.2f },
        { "sparse  n=5000  b=0.2 l=0.0",   123,  5000,  0.2f, 0.0f },
        { "default n=5000  b=0.45 l=0.08", 123,  5000, 0.45f, 0.08f },
        { "dense   n=5000  b=0.9 l=0.2",   123,  5000,  0.9f, 0.2f },
        { "sparse  n=20000 b=0.2 l=0.0",   456, 20000,  0.2f, 0.0f },
        { "default n=20000 b=0.45 l=0.08", 456, 20000, 0.45f, 0.08f },
        { "dense   n=20000 b=0.9 l=0.2",   456, 20000,  0.9f, 0.2f },
        { "sparse  n=50000  b=0.2 l=0.0",    789,  50000,  0.2f, 0.0f },
        { "default n=50000  b=0.45 l=0.08",  789,  50000, 0.45f, 0.08f },
        { "dense   n=50000  b=0.9 l=0.2",    789,  50000,  0.9f, 0.2f },
        { "sparse  n=100000 b=0.2 l=0.0",   1000, 100000,  0.2f, 0.0f },
        { "default n=100000 b=0.45 l=0.08", 1000, 100000, 0.45f, 0.08f },
        { "dense   n=100000 b=0.9 l=0.2",   1000, 100000,  0.9f, 0.2f },
        { "sparse  n=250000 b=0.2 l=0.0",   2000, 250000,  0.2f, 0.0f },
        { "default n=250000 b=0.45 l=0.08", 2000, 250000, 0.45f, 0.08f },
        { "dense   n=250000 b=0.9 l=0.2",   2000, 250000,  0.9f, 0.2f },
        { "sparse  n=500000 b=0.2 l=0.0",   3000, 500000,  0.2f, 0.0f },
        { "default n=500000 b=0.45 l=0.08", 3000, 500000, 0.45f, 0.08f },
        { "dense   n=500000 b=0.9 l=0.2",   3000, 500000,  0.9f, 0.2f },
        { "sparse  n=1000000 b=0.2 l=0.0",   4000, 1000000,  0.2f, 0.0f },
        { "default n=1000000 b=0.45 l=0.08", 4000, 1000000, 0.45f, 0.08f },
        { "dense   n=1000000 b=0.9 l=0.2",   4000, 1000000,  0.9f, 0.2f },
    };

    for (size_t i = 0; i < sizeof(scenarios) / sizeof(*scenarios); i++)
        run(&scenarios[i]);

    return 0;
}
