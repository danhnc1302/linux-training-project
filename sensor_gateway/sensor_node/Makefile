CC     = gcc
CFLAGS = -Wall -Wextra -g

TARGET = sensor_node

all: $(TARGET)

$(TARGET): sensor_node.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
