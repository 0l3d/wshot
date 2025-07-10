CC = gcc
CFLAGS = -O2 -march=native
LDFLAGS = -lwayland-client -lpng
TARGET = wshot
SOURCES = wshot.c $(SOURCE_FILE)
OBJECTS = $(SOURCES:.c=.o)

# PROTOCOL
PROTOCOL_XML = ./protocol/wlr-screencopy-unstable-v1.xml
HEADER_FILE = wlr-screencopy-unstable-v1.h
SOURCE_FILE = wlr-screencopy-unstable-v1.c

all: $(HEADER_FILE) $(SOURCE_FILE) $(TARGET) cleansrc

$(HEADER_FILE):
	wayland-scanner client-header $(PROTOCOL_XML) $(HEADER_FILE)

$(SOURCE_FILE):
	wayland-scanner private-code $(PROTOCOL_XML) $(SOURCE_FILE)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

cleansrc:
	rm -f $(OBJECTS) $(SOURCE_FILE) $(HEADER_FILE)


clean:
	rm -f $(OBJECTS) $(TARGET)
