CC = gcc
CFLAGS = -g -O2 -march=native
LDFLAGS = -lwayland-client -lpng
TARGET = wshot
SOURCES = wshot.c $(SOURCE_FILE) $(XDG_SOURCE)
OBJECTS = $(SOURCES:.c=.o)

# PROTOCOL
PROTOCOL_XML = ./protocol/wlr-screencopy-unstable-v1.xml
XDG_PROTOCOL = ./protocol/xdg-output-unstable-v1.xml
HEADER_FILE = wlr-screencopy-unstable-v1.h
SOURCE_FILE = wlr-screencopy-unstable-v1.c
XDG_HEADER = xdg-output-unstable-v1.h
XDG_SOURCE = xdg-output-unstable-v1.c

all: $(HEADER_FILE) $(SOURCE_FILE) $(XDG_HEADER) $(XDG_SOURCE) $(TARGET) # cleansrc

$(HEADER_FILE):
	wayland-scanner client-header $(PROTOCOL_XML) $(HEADER_FILE)

$(SOURCE_FILE):
	wayland-scanner private-code $(PROTOCOL_XML) $(SOURCE_FILE)

$(XDG_HEADER):
	wayland-scanner client-header $(XDG_PROTOCOL) $(XDG_HEADER)
$(XDG_SOURCE):
	wayland-scanner private-code $(XDG_PROTOCOL) $(XDG_SOURCE)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

cleansrc:
	rm -f $(OBJECTS) $(SOURCE_FILE) $(HEADER_FILE)


clean:
	rm -f $(OBJECTS) $(TARGET)
