CC = g++
CFLAGS  = -Wall
TARGET = kingcon

all: $(TARGET)

$(TARGET): $(TARGET).cpp
	$(CC) $(CFLAGS) -o $@ $^ -lfreeimage

clean:
	$(RM) $(TARGET)

.PHONY: clean