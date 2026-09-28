CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11
TARGET = mishell
SRCS = mishell.c

$(TARGET):$(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)