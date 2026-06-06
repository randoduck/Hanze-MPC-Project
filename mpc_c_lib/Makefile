CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L -Iinclude
LDFLAGS = -lpthread

BUILDDIR = build

OBJS = $(BUILDDIR)/mpc_field.o \
       $(BUILDDIR)/mpc_share.o \
       $(BUILDDIR)/mpc_comm_tcp.o

all: $(BUILDDIR) \
     $(BUILDDIR)/test_field \
     $(BUILDDIR)/test_share \
     $(BUILDDIR)/test_uc1 \
     $(BUILDDIR)/test_operator

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/mpc_field.o: src/mpc_field.c include/mpc_field.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/mpc_share.o: src/mpc_share.c include/mpc_share.h include/mpc_field.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/mpc_comm_tcp.o: src/mpc_comm_tcp.c include/mpc_comm.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/test_field: test/test_field.c $(BUILDDIR)/mpc_field.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILDDIR)/test_share: test/test_share.c $(BUILDDIR)/mpc_field.o $(BUILDDIR)/mpc_share.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILDDIR)/test_uc1: test/test_uc1.c $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/test_operator: test/test_operator.c $(BUILDDIR)/mpc_field.o
	$(CC) $(CFLAGS) $^ -o $@

test: $(BUILDDIR)/test_field $(BUILDDIR)/test_share
	@echo ""
	@echo "=== test_field ==="
	./$(BUILDDIR)/test_field
	@echo ""
	@echo "=== test_share ==="
	./$(BUILDDIR)/test_share
	@echo ""
	@echo "All standalone tests done."

clean:
	rm -f $(BUILDDIR)/*.o $(BUILDDIR)/test_field $(BUILDDIR)/test_share \
	      $(BUILDDIR)/test_uc1 $(BUILDDIR)/test_operator

.PHONY: all test clean
