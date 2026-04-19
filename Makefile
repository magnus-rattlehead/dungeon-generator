CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2
DBGFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O0 -g \
           -fsanitize=address,undefined -fno-omit-frame-pointer
TARGET   = dungeon
TEST_BIN = test_compression
SRCS      = dungeon.c main.c
HDRS      = dungeon.h
BENCH_BIN = bench_compression

.PHONY: all debug test unit-test bench clean install

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lz

$(TEST_BIN): test_compression.c dungeon.c $(HDRS)
	$(CC) $(DBGFLAGS) -o $@ test_compression.c dungeon.c -lz

$(BENCH_BIN): bench_compression.c dungeon.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ bench_compression.c dungeon.c -lz

bench: $(BENCH_BIN)
	./$(BENCH_BIN) 2>/dev/null

unit-test: $(TEST_BIN)
	@echo "=== Unit tests (chunk compression) ==="
	./$(TEST_BIN) 2>&1 | grep -v "^Seed:"
	@echo ""

debug: $(SRCS) $(HDRS)
	$(CC) $(DBGFLAGS) -o $(TARGET)_debug $(SRCS)

test: $(TARGET) unit-test
	@echo "=== Smoke test (100 rooms) ==="
	./$(TARGET) -n 100 -s 42 -o /tmp/dg_a.txt
	@echo "=== Reproducibility: same seed must produce identical output ==="
	./$(TARGET) -n 100 -s 42 -o /tmp/dg_b.txt
	diff /tmp/dg_a.txt /tmp/dg_b.txt \
		&& echo "PASS: reproducibility" \
		|| (echo "FAIL: outputs differ for same seed" && exit 1)
	@echo "=== Different seed must produce different output ==="
	./$(TARGET) -n 100 -s 43 -o /tmp/dg_c.txt
	diff /tmp/dg_a.txt /tmp/dg_c.txt \
		&& echo "WARN: seeds 42 and 43 produced identical output" \
		|| echo "PASS: different seeds produce different dungeons"
	@echo "=== Dense dungeon (branch=0.9, loops=0.2) ==="
	./$(TARGET) -n 80 -s 1 -b 0.9 -l 0.2 -o /tmp/dg_dense.txt
	@echo "=== Winding corridors (branch=0.2, loops=0.0) ==="
	./$(TARGET) -n 80 -s 1 -b 0.2 -l 0.0 -o /tmp/dg_winding.txt
	@echo "=== Large dungeon (1000 rooms) ==="
	./$(TARGET) -n 1000 -s 99 -o /tmp/dg_large.txt
	@echo "All tests passed."

clean:
	rm -f $(TARGET) $(TARGET)_debug $(BENCH_BIN) \
	      /tmp/dg_a.txt /tmp/dg_b.txt /tmp/dg_c.txt \
	      /tmp/dg_dense.txt /tmp/dg_winding.txt /tmp/dg_large.txt

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
