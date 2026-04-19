#include "dungeon.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  -n, --rooms N          Target room count (default 64)\n"
        "  -s, --seed S           RNG seed (default 0 = time-based; always printed\n"
        "                           to stderr so you can reproduce any run)\n"
        "  -b, --branch F         Branch factor 0.0-1.0 (default 0.45)\n"
        "                           0.0 = winding single corridor\n"
        "                           1.0 = dense grid\n"
        "  -l, --loops F          Loop factor 0.0-1.0 (default 0.08)\n"
        "                           Higher values add more cycles to the graph\n"
        "  -o, --output FILE      Write ASCII map to FILE (default: stdout)\n"
        "  -p, --print-rooms      Print one line per room to stdout\n"
        "  -i, --infinite         Infinite mode: generate on-demand by viewport\n"
        "                           Requires --viewport; --rooms is ignored\n"
        "  -V, --viewport X0,Y0,X1,Y1\n"
        "                         Render only this world-coordinate rectangle.\n"
        "                         In infinite mode, also controls which chunks\n"
        "                         are generated. Coordinates are room indices.\n"
        "  -h, --help             Show this help\n"
        "\n"
        "Examples:\n"
        "  %s -n 100 -s 42                              # reproducible 100-room dungeon\n"
        "  %s -n 200 -b 0.8 -l 0.2 -o dungeon.txt      # dense, loopy dungeon to file\n"
        "  %s -n 50  -b 0.2 -l 0.0                     # winding corridors\n"
        "  %s -i -s 42 -V -31,-31,31,31                 # infinite mode, 63x63 viewport\n"
        "  %s -i -s 42 -V -63,-63,63,63 -o big.txt      # larger infinite viewport\n",
        prog, prog, prog, prog, prog, prog);
}

/* Parse a non-negative integer. Returns false and prints an error on failure. */
static bool parse_int(const char *prog, const char *flag,
                       const char *s, int *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || end == s || errno != 0 || v < 0 || v > 1000000000) {
        fprintf(stderr, "%s: invalid value for %s: '%s'\n", prog, flag, s);
        return false;
    }
    *out = (int)v;
    return true;
}

/* Parse a uint64 seed. Returns false on error. */
static bool parse_seed(const char *prog, const char *s, uint64_t *out) {
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (*end != '\0' || end == s || errno != 0) {
        fprintf(stderr, "%s: invalid seed value: '%s'\n", prog, s);
        return false;
    }
    *out = (uint64_t)v;
    return true;
}

/* Parse viewport "x0,y0,x1,y1". */
static bool parse_viewport(const char *prog, const char *s,
                             int *x0, int *y0, int *x1, int *y1) {
    /* sscanf is safe here: %d with explicit pointers, no overflow risk */
    if (sscanf(s, "%d,%d,%d,%d", x0, y0, x1, y1) != 4) {
        fprintf(stderr, "%s: invalid --viewport '%s' (expected X0,Y0,X1,Y1)\n",
                prog, s);
        return false;
    }
    if (*x1 < *x0 || *y1 < *y0) {
        fprintf(stderr, "%s: --viewport: x1 must be >= x0 and y1 must be >= y0\n",
                prog);
        return false;
    }
    return true;
}

/* Parse a float in [0, 1]. Returns false on error. */
static bool parse_float01(const char *prog, const char *flag,
                            const char *s, float *out) {
    char *end;
    errno = 0;
    float v = strtof(s, &end);
    if (*end != '\0' || end == s || errno != 0 || v < 0.0f || v > 1.0f) {
        fprintf(stderr, "%s: invalid value for %s: '%s' (must be 0.0-1.0)\n",
                prog, flag, s);
        return false;
    }
    *out = v;
    return true;
}

int main(int argc, char *argv[]) {
    DungeonConfig cfg   = dungeon_default_config();
    const char *outfile = NULL;
    bool print_rooms    = false;
    bool infinite_mode  = false;
    bool has_viewport   = false;
    int vx0 = 0, vy0 = 0, vx1 = 0, vy1 = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(a, "-p") == 0 || strcmp(a, "--print-rooms") == 0) {
            print_rooms = true;
            continue;
        }
        if (strcmp(a, "-i") == 0 || strcmp(a, "--infinite") == 0) {
            infinite_mode = true;
            continue;
        }

        /* All remaining flags take exactly one value argument. */
        const char *val = NULL;

        /* Support --flag=value */
        const char *eq = strchr(a, '=');
        if (eq && a[0] == '-' && a[1] == '-') {
            val = eq + 1;
        } else if (i + 1 < argc) {
            val = argv[++i];
        } else {
            fprintf(stderr, "%s: option '%s' requires a value\n", argv[0], a);
            usage(argv[0]);
            return 1;
        }

        if (strncmp(a, "-n", 2) == 0 || strncmp(a, "--rooms", 7) == 0) {
            if (!parse_int(argv[0], a, val, &cfg.num_rooms)) return 1;
        } else if (strncmp(a, "-s", 2) == 0 || strncmp(a, "--seed", 6) == 0) {
            if (!parse_seed(argv[0], val, &cfg.seed)) return 1;
        } else if (strncmp(a, "-b", 2) == 0 || strncmp(a, "--branch", 8) == 0) {
            if (!parse_float01(argv[0], a, val, &cfg.branch_factor)) return 1;
        } else if (strncmp(a, "-l", 2) == 0 || strncmp(a, "--loops", 7) == 0) {
            if (!parse_float01(argv[0], a, val, &cfg.loop_factor)) return 1;
        } else if (strncmp(a, "-o", 2) == 0 || strncmp(a, "--output", 8) == 0) {
            outfile = val;
        } else if (strncmp(a, "-V", 2) == 0 || strncmp(a, "--viewport", 10) == 0) {
            if (!parse_viewport(argv[0], val, &vx0, &vy0, &vx1, &vy1)) return 1;
            has_viewport = true;
        } else {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], a);
            usage(argv[0]);
            return 1;
        }
    }

    /* Infinite mode requires an explicit viewport. */
    if (infinite_mode && !has_viewport) {
        fprintf(stderr,
            "%s: --infinite requires --viewport X0,Y0,X1,Y1\n"
            "Example: --infinite --viewport -31,-31,31,31\n",
            argv[0]);
        return 1;
    }

    /* In infinite mode num_rooms is irrelevant; set to 0 to signal it. */
    if (infinite_mode) cfg.num_rooms = 0;

    Dungeon *d = dungeon_create(cfg);
    if (!d) {
        fprintf(stderr, "%s: dungeon generation failed (out of memory)\n", argv[0]);
        return 1;
    }

    /* Infinite mode: generate all chunks that cover the viewport. */
    if (infinite_mode) {
        if (!dungeon_extend_viewport(d, vx0, vy0, vx1, vy1)) {
            fprintf(stderr, "%s: chunk generation failed (out of memory)\n", argv[0]);
            dungeon_destroy(d);
            return 1;
        }
        /* Compress rooms outside the viewport to reclaim memory. */
        int freed = dungeon_compress_distant(d, vx0, vy0, vx1, vy1);
        fprintf(stderr, "Compressed %d rooms outside viewport.\n", freed);
    }

    if (print_rooms) {
        /* Cap room listing in infinite mode to avoid flooding the terminal. */
        int print_max = infinite_mode ? 200 : 0;
        dungeon_print_rooms(d, print_max);
    }

    /* Determine render bounds: explicit viewport > full bounding box. */
    char *map;
    if (has_viewport) {
        map = dungeon_render(d, vx0, vy0, vx1, vy1);
    } else {
        map = dungeon_render_full(d);
    }

    if (!map) {
        fprintf(stderr,
            "%s: render failed — viewport may exceed DUNGEON_RENDER_MAX_DIM (%d).\n"
            "Use --viewport to specify a smaller area.\n",
            argv[0], DUNGEON_RENDER_MAX_DIM);
        dungeon_destroy(d);
        return 1;
    }

    if (outfile) {
        FILE *f = fopen(outfile, "w");
        if (!f) {
            perror(outfile);
            free(map);
            dungeon_destroy(d);
            return 1;
        }
        fputs(map, f);
        fclose(f);
    } else {
        fputs(map, stdout);
    }

    int bx0, by0, bx1, by1;
    dungeon_bounds(d, &bx0, &by0, &bx1, &by1);
    fprintf(stderr, "Rooms: %d  Bounds: (%d,%d)..(%d,%d)%s\n",
            dungeon_room_count(d), bx0, by0, bx1, by1,
            outfile ? "" : "  (map above)");

    free(map);
    dungeon_destroy(d);
    return 0;
}
