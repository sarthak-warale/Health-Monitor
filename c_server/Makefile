# ============================================================
# Health Monitor - Software Server Makefile (MinGW / Windows)
# ============================================================

CC      = gcc
CFLAGS  = -std=c99 -Wall -O2 -DWIN32_LEAN_AND_MEAN -D__USE_MINGW_ANSI_STDIO=1
LDFLAGS = -lws2_32

SRCS = server.c
TARGET = server.exe

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)
	@echo.
	@echo  Build successful -^> $(TARGET)
	@echo  Run with: .\$(TARGET)
	@echo.

run: $(TARGET)
	.\$(TARGET)

clean:
	del /f *.o $(TARGET) 2>nul || true

.PHONY: all run clean
