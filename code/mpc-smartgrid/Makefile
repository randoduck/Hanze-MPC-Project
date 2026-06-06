CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L -Iinclude
LDFLAGS = -lpthread

BUILDDIR = build

OBJS = $(BUILDDIR)/mpc_field.o \
       $(BUILDDIR)/mpc_share.o \
       $(BUILDDIR)/mpc_comm_tcp.o \
       $(BUILDDIR)/mpc_runner.o

TESTS = $(BUILDDIR)/test_field \
        $(BUILDDIR)/test_share \
        $(BUILDDIR)/test_uc1 \
        $(BUILDDIR)/bench_mpc \
        $(BUILDDIR)/meter_node \
        $(BUILDDIR)/secure_agg_server \
        $(BUILDDIR)/secure_agg_meter

all: $(BUILDDIR) $(TESTS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/mpc_field.o: src/mpc_field.c include/mpc_field.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/mpc_share.o: src/mpc_share.c include/mpc_share.h include/mpc_field.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/mpc_comm_tcp.o: src/mpc_comm_tcp.c include/mpc_comm.h include/mpc_limits.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/mpc_runner.o: src/mpc_runner.c include/mpc_runner.h include/mpc_field.h include/mpc_share.h include/mpc_comm.h include/mpc_limits.h include/mpc_status.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/test_field: test/test_field.c $(BUILDDIR)/mpc_field.o | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILDDIR)/test_share: test/test_share.c $(BUILDDIR)/mpc_field.o $(BUILDDIR)/mpc_share.o | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILDDIR)/test_uc1: test/test_uc1.c $(BUILDDIR)/mpc_field.o $(BUILDDIR)/mpc_share.o $(BUILDDIR)/mpc_comm_tcp.o | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/bench_mpc: test/bench_mpc.c $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/meter_node: apps/meter_node.c $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/secure_agg_server: apps/secure_agg_server.c $(BUILDDIR)/mpc_field.o include/secure_agg.h include/mpc_limits.h | $(BUILDDIR)
	$(CC) $(CFLAGS) apps/secure_agg_server.c $(BUILDDIR)/mpc_field.o -o $@ $(LDFLAGS)

$(BUILDDIR)/secure_agg_meter: apps/secure_agg_meter.c $(BUILDDIR)/mpc_field.o include/secure_agg.h | $(BUILDDIR)
	$(CC) $(CFLAGS) apps/secure_agg_meter.c $(BUILDDIR)/mpc_field.o -o $@ $(LDFLAGS)

test: $(BUILDDIR)/test_field $(BUILDDIR)/test_share
	@echo ""
	@echo "=== test_field ==="
	./$(BUILDDIR)/test_field
	@echo ""
	@echo "=== test_share ==="
	./$(BUILDDIR)/test_share
	@echo ""
	@echo "All standalone tests done."

# ---- Build-mode convenience targets -------------------------------------
# Each does a clean rebuild with mode-specific flags via a recursive make,
# so the flags reliably apply to every object regardless of build order.

debug:
	$(MAKE) clean
	$(MAKE) all CFLAGS="-Wall -Wextra -O0 -g -std=c99 -D_POSIX_C_SOURCE=200809L -Iinclude"

# ASan/UBSan. Practical on PC 5; may be too heavy for Pi 2.
asan:
	$(MAKE) clean
	$(MAKE) all \
	  CFLAGS="-Wall -Wextra -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c99 -D_POSIX_C_SOURCE=200809L -Iinclude" \
	  LDFLAGS="-lpthread -fsanitize=address,undefined"

release:
	$(MAKE) clean
	$(MAKE) all CFLAGS="-Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L -Iinclude"

# ---- Formatting and static analysis -------------------------------------

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not installed; skipping"; exit 0; }
	clang-format -i include/*.h src/*.c apps/*.c test/*.c
	@echo "formatted."

lint:
	@command -v cppcheck >/dev/null 2>&1 || { echo "cppcheck not installed; skipping"; exit 0; }
	cppcheck --enable=all --inconclusive --std=c99 -Iinclude src apps test

clean:
	rm -f $(BUILDDIR)/*.o \
	      $(BUILDDIR)/test_field \
	      $(BUILDDIR)/test_share \
	      $(BUILDDIR)/test_uc1 \
	      $(BUILDDIR)/bench_mpc \
	      $(BUILDDIR)/meter_node \
	      $(BUILDDIR)/secure_agg_server \
	      $(BUILDDIR)/secure_agg_meter

.PHONY: all test debug asan release format lint clean
