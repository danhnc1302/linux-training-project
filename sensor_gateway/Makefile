CC = gcc

CFLAGS = -Wall -Wextra -g -pthread -Iinclude -Iinclude/adapter
LDLIBS = -pthread -lsqlite3

# make DEBUG=1  -> print every measurement (Step 0: debug mode by macro)
ifeq ($(DEBUG),1)
CFLAGS += -DDEBUG
endif

SRC = $(wildcard src/*.c) $(wildcard src/adapter/*.c)
OBJ = $(SRC:.c=.o)
ADAPTER_SRC = $(wildcard src/adapter/*.c)

TARGET = server
ARGS = 1234 8080

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ---- unit tests (Step 4, 5, 6) ----
test: test/test_parsers test/test_datamgr test/test_storagemgr
	./test/test_parsers
	./test/test_datamgr
	./test/test_storagemgr

test/test_parsers: test/test_parsers.c $(ADAPTER_SRC)
	$(CC) $(CFLAGS) $^ -o $@

test/test_datamgr: test/test_datamgr.c src/datamgr.c
	$(CC) $(CFLAGS) $^ -o $@

test/test_storagemgr: test/test_storagemgr.c src/storagemgr.c src/sbuffer.c src/gateway.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

# ---- Step 7 verification (stop with Ctrl+C) ----
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=definite,indirect \
	         --trace-children=yes --log-file=valgrind_%p.txt ./$(TARGET) $(ARGS)

helgrind: $(TARGET)
	valgrind --tool=helgrind --log-file=helgrind.txt ./$(TARGET) $(ARGS)

clean:
	rm -f $(OBJ) $(TARGET)
	rm -f test/test_parsers test/test_datamgr test/test_storagemgr
	rm -f gateway.log logFifo

.PHONY: all clean test valgrind helgrind
