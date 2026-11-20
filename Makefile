CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic -O2 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_XOPEN_SOURCE_EXTENDED=1
LDFLAGS ?= -lncursesw

SRC_DIR := src
BIN_DIR := bin
OBJ_DIR := obj
TEST_DIR := tests
MAN_DIR := man

# Centralized version extraction from src/version.h
VERSION ?= $(shell grep MDE_VERSION_STRING src/version.h | cut -d'"' -f2)
ARCH_RAW ?= $(shell dpkg --print-architecture 2>/dev/null || uname -m)
ifeq ($(ARCH_RAW),x86_64)
    DEB_ARCH := amd64
else ifeq ($(ARCH_RAW),aarch64)
    DEB_ARCH := arm64
else
    DEB_ARCH := $(ARCH_RAW)
endif

DEB_PKG_NAME := mde_$(VERSION)_$(DEB_ARCH)
DEB_STAGING  := $(BIN_DIR)/$(DEB_PKG_NAME)

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
TARGET := $(BIN_DIR)/mde

# Filter out main.o when linking test runner
LIB_OBJS := $(filter-out $(OBJ_DIR)/main.o, $(OBJS))

.PHONY: all clean test install deb

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

test: $(LIB_OBJS) tests/test_runner.c | $(BIN_DIR)
	$(CC) $(CFLAGS) tests/test_runner.c $(LIB_OBJS) $(LDFLAGS) -o $(BIN_DIR)/test_runner
	./$(BIN_DIR)/test_runner

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

install: $(TARGET)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/mde
	install -d $(DESTDIR)/usr/local/share/man/man1
	install -m 644 $(MAN_DIR)/mde.1 $(DESTDIR)/usr/local/share/man/man1/mde.1

deb: $(TARGET)
	@echo "==> Packaging Debian package $(DEB_PKG_NAME).deb..."
	rm -rf $(DEB_STAGING)
	mkdir -p $(DEB_STAGING)/DEBIAN
	mkdir -p $(DEB_STAGING)/usr/bin
	mkdir -p $(DEB_STAGING)/usr/share/man/man1
	mkdir -p $(DEB_STAGING)/usr/share/doc/mde
	# Binary installation
	install -m 755 $(TARGET) $(DEB_STAGING)/usr/bin/mde
	strip $(DEB_STAGING)/usr/bin/mde
	# Man page installation with gzip
	gzip -9 -c $(MAN_DIR)/mde.1 > $(DEB_STAGING)/usr/share/man/man1/mde.1.gz
	chmod 644 $(DEB_STAGING)/usr/share/man/man1/mde.1.gz
	# Documentation & copyright
	install -m 644 README.md $(DEB_STAGING)/usr/share/doc/mde/README.md
	@echo "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/" > $(DEB_STAGING)/usr/share/doc/mde/copyright
	@echo "Upstream-Name: mde" >> $(DEB_STAGING)/usr/share/doc/mde/copyright
	@echo "Source: https://github.com/sikvall/mde" >> $(DEB_STAGING)/usr/share/doc/mde/copyright
	@echo "" >> $(DEB_STAGING)/usr/share/doc/mde/copyright
	@echo "Files: *" >> $(DEB_STAGING)/usr/share/doc/mde/copyright
	@echo "Copyright: 2026 Anders" >> $(DEB_STAGING)/usr/share/doc/mde/copyright
	@echo "License: MIT" >> $(DEB_STAGING)/usr/share/doc/mde/copyright
	# Debian control file
	@echo "Package: mde" > $(DEB_STAGING)/DEBIAN/control
	@echo "Version: $(VERSION)" >> $(DEB_STAGING)/DEBIAN/control
	@echo "Section: editors" >> $(DEB_STAGING)/DEBIAN/control
	@echo "Priority: optional" >> $(DEB_STAGING)/DEBIAN/control
	@echo "Architecture: $(DEB_ARCH)" >> $(DEB_STAGING)/DEBIAN/control
	@echo "Maintainer: Anders <anders@localhost>" >> $(DEB_STAGING)/DEBIAN/control
	@echo "Depends: libc6, libncursesw6 (>= 6)" >> $(DEB_STAGING)/DEBIAN/control
	@echo "Suggests: pandoc" >> $(DEB_STAGING)/DEBIAN/control
	@echo "Description: Full-screen text-mode Markdown editor" >> $(DEB_STAGING)/DEBIAN/control
	@echo " mde is a specialized terminal text editor designed for editing and viewing" >> $(DEB_STAGING)/DEBIAN/control
	@echo " Markdown (.md) documents with dual-pane live preview, in-place rich" >> $(DEB_STAGING)/DEBIAN/control
	@echo " syntax styling, column ruler, task checkboxes, and UTF-8 support." >> $(DEB_STAGING)/DEBIAN/control
	# Set standard Debian directory and file permissions
	find $(DEB_STAGING) -type d -exec chmod 755 {} +
	find $(DEB_STAGING) -type f -exec chmod 644 {} +
	chmod 755 $(DEB_STAGING)/usr/bin/mde
	# Build the Debian package
	dpkg-deb --build --root-owner-group $(DEB_STAGING) $(BIN_DIR)/$(DEB_PKG_NAME).deb
	rm -rf $(DEB_STAGING)
	@echo "==> Debian package generated at: $(BIN_DIR)/$(DEB_PKG_NAME).deb"
