# dungeon-generator

A procedural dungeon generator in C. Produces an ASCII-rendered room graph where each room connects to up to four cardinal neighbours. Generation uses BFS with a seeded xorshift64 RNG — the same seed always produces the same dungeon.

## Build

```
make          # release build → ./dungeon
make debug    # ASan + UBSan build → ./dungeon_debug
make test     # build + run all tests (unit + CLI smoke tests)
make install  # install to /usr/local/bin/dungeon
```

Requires a C11 compiler and zlib (ships with macOS; `apt install zlib1g-dev` on Debian/Ubuntu).

## Usage

```
dungeon [OPTIONS]

  -n, --rooms N          Target room count (default 64)
  -s, --seed S           RNG seed (default: time-based, always printed to stderr)
  -b, --branch F         Branch factor 0.0–1.0 (default 0.45)
                           0.0 = winding single corridor, 1.0 = dense grid
  -l, --loops F          Loop factor 0.0–1.0 (default 0.08)
  -o, --output FILE      Write ASCII map to FILE (default: stdout)
  -p, --print-rooms      Print one line per room to stdout
  -i, --infinite         Infinite mode: generate on-demand by viewport
                           Requires --viewport; --rooms is ignored
  -V, --viewport X0,Y0,X1,Y1
                         Render only this world-coordinate rectangle
  -h, --help             Show help
```

### Examples

```sh
# Reproducible 100-room dungeon
dungeon -n 100 -s 42

# Dense, loopy dungeon written to a file
dungeon -n 200 -b 0.8 -l 0.2 -o dungeon.txt

# Winding corridors, no loops
dungeon -n 50 -b 0.2 -l 0.0

# Infinite mode — generate and render a 63×63 viewport
dungeon -i -s 42 -V -31,-31,31,31
```

### Sample output (`-n 40 -s 42`)

```
# # #      
    | | |      
  #-# # # #    
    | | | |    
    #-# #-#    
      | |      
  # #-# #      
  | | | |      
#-# # #-#   #  
  | | | |   |  
  #-#-# #-#-#-#
  | | | |      
  #-# # #-#-#  
  |   |   |    
  # #-#   #    
    |          
    #
```

## API

The generator can be used as a library via `dungeon.h`:

```c
// Basic usage
DungeonConfig cfg = dungeon_default_config();
cfg.num_rooms = 100;
cfg.seed      = 42;
Dungeon *d = dungeon_create(cfg);
char *map = dungeon_render_full(d);
puts(map);
free(map);
dungeon_destroy(d);

// Infinite / chunked mode
cfg.num_rooms = 0;
Dungeon *d = dungeon_create(cfg);
dungeon_extend_viewport(d, -63, -63, 63, 63);
char *map = dungeon_render(d, -63, -63, 63, 63);
dungeon_compress_distant(d, -63, -63, 63, 63);  // free memory outside viewport
```

### Key functions

| Function | Description |
|---|---|
| `dungeon_create(cfg)` | Generate a dungeon (or empty shell for infinite mode) |
| `dungeon_extend(d, cx, cy)` | Generate chunk at grid coordinates `(cx, cy)` |
| `dungeon_extend_viewport(d, x0,y0,x1,y1)` | Extend all chunks overlapping a world-coord rectangle |
| `dungeon_compress_distant(d, x0,y0,x1,y1)` | Compress chunks outside a viewport; returns rooms freed |
| `dungeon_render(d, x0,y0,x1,y1)` | Render a viewport to a heap-allocated string |
| `dungeon_render_full(d)` | Render the full bounding box |
| `dungeon_bounds(d, ...)` | Get the world-coordinate bounding box |
| `dungeon_destroy(d)` | Free all memory |

## Memory model

Rooms are stored in a hash-mapped chunk grid (32×32 rooms per chunk). Chunks outside the active viewport can be compressed with `dungeon_compress_distant`: live `Room` structs are serialised into a fixed 384-byte bitmap and deflated with zlib (~150–200 bytes per dense chunk), then decompressed on demand during rendering. This makes infinite-mode dungeons viable with bounded memory.
