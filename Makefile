TARGET = kingcon

CC = g++
CFLAGS  = -Wall
LIBS = -lfreeimage

all: $(TARGET)

$(TARGET): $(TARGET).cpp
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	$(RM) $(TARGET)

.PHONY: clean