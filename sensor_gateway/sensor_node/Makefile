CC      = gcc
CFLAGS  = -Wall -Wextra -g

TARGET  = sensor_node
SRC     = sensor_node.c
OBJ     = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@

clean:
	rm -f $(OBJ)
	rm -f $(TARGET)

.PHONY: all clean