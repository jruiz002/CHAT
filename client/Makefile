CC      = gcc
CFLAGS  = -Wall -Wextra -g -I..
LDFLAGS = -lpthread

TARGET  = cliente
SRC     = client.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) ../protocol.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
