CC = x86_64-w64-mingw32-gcc-posix
WINDRES = x86_64-w64-mingw32-windres
CFLAGS = -O2 -std=c99 -Wall -Wextra
LDFLAGS = -mwindows
LIBS =

SRC = src/main.c src/gui.c src/midi_parser.c src/note_mapper.c src/key_input.c src/playback.c
OBJ = $(SRC:.c=.o)
TARGET = midi2gipiano.exe

LIBS += -lwinmm
RC_OBJ = resources/resource.o

all: $(TARGET)

$(TARGET): $(OBJ) $(RC_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

resources/resource.o: resources/resource.rc resources/app.manifest
	$(WINDRES) -I. -o $@ resources/resource.rc

.c.o:
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

clean:
	rm -f $(OBJ) resources/resource.o $(TARGET)

.PHONY: all clean
