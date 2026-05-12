# Makefile — TOCTOU Race Condition Tester
#
# Targets:
#   make          — plain HTTP build
#   make tls      — HTTPS/TLS build (requires libssl-dev)
#   make clean    — remove binaries
#   make install  — copy binary to /usr/local/bin
#
# Install TLS headers on Kali/Debian:
#   sudo apt install libssl-dev

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -pthread
SRCS    = main.c conn.c http.c worker.c output.c request_build.c sha256.c
TARGET  = race_tester

.PHONY: all tls clean install

all: $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)
	@echo "Built: ./$(TARGET)  (plain HTTP)"

tls: $(SRCS)
	$(CC) $(CFLAGS) -DWITH_TLS -o $(TARGET) $(SRCS) -lssl -lcrypto
	@echo "Built: ./$(TARGET)  (HTTP + TLS)"

clean:
	rm -f $(TARGET)

install: all
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	@echo "Installed to /usr/local/bin/$(TARGET)"
