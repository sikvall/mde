CC ?= gcc
CFLAGS ?= -std=c11 -MMD -MP -Wall -Wextra -pedantic -O2 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_XOPEN_SOURCE_EXTENDED=1
LDFLAGS ?= -lncursesw -lzip

PREFIX ?= /usr/local
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
MD2EPUB_TARGET := $(BIN_DIR)/md2epub

# Objects for each binary
MDE_OBJS := $(filter-out $(OBJ_DIR)/md2epub_main.o, $(OBJS))
MD2EPUB_OBJS := $(OBJ_DIR)/md2epub_main.o $(OBJ_DIR)/export_epub.o $(OBJ_DIR)/export.o \
                $(OBJ_DIR)/export_html.o $(OBJ_DIR)/export_xml.o $(OBJ_DIR)/export_latex.o \
                $(OBJ_DIR)/buffer.o $(OBJ_DIR)/markdown.o $(OBJ_DIR)/utf8.o $(OBJ_DIR)/undo.o

# Filter out main files when linking test runner
LIB_OBJS := $(filter-out $(OBJ_DIR)/main.o $(OBJ_DIR)/md2epub_main.o, $(OBJS))

.PHONY: all clean test install deb

all: $(TARGET) $(MD2EPUB_TARGET)

$(TARGET): $(MDE_OBJS) | $(BIN_DIR)
	$(CC) $(MDE_OBJS) $(LDFLAGS) -o $@

$(MD2EPUB_TARGET): $(MD2EPUB_OBJS) | $(BIN_DIR)
	$(CC) $(MD2EPUB_OBJS) -lzip -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

test: $(LIB_OBJS) tests/test_runner.c | $(BIN_DIR)
	$(CC) $(CFLAGS) tests/test_runner.c $(LIB_OBJS) $(LDFLAGS) -o $(BIN_DIR)/test_runner
	./$(BIN_DIR)/test_runner

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

install: $(TARGET) $(MD2EPUB_TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/mde
	install -m 755 $(MD2EPUB_TARGET) $(DESTDIR)$(PREFIX)/bin/md2epub
	install -d $(DESTDIR)$(PREFIX)/share/man/man1
	install -m 644 $(MAN_DIR)/mde.1 $(DESTDIR)$(PREFIX)/share/man/man1/mde.1
	install -m 644 $(MAN_DIR)/md2epub.1 $(DESTDIR)$(PREFIX)/share/man/man1/md2epub.1

deb: $(TARGET) $(MD2EPUB_TARGET)
	@echo "==> Packaging Debian package $(DEB_PKG_NAME).deb..."
	rm -rf $(DEB_STAGING)
	mkdir -p $(DEB_STAGING)/DEBIAN
	mkdir -p $(DEB_STAGING)/usr/bin
	mkdir -p $(DEB_STAGING)/usr/share/man/man1
	mkdir -p $(DEB_STAGING)/usr/share/doc/mde
	# Binary installation
	install -m 755 $(TARGET) $(DEB_STAGING)/usr/bin/mde
	install -m 755 $(MD2EPUB_TARGET) $(DEB_STAGING)/usr/bin/md2epub
	strip $(DEB_STAGING)/usr/bin/mde
	strip $(DEB_STAGING)/usr/bin/md2epub
	# Man page installation with gzip
	gzip -9 -c $(MAN_DIR)/mde.1 > $(DEB_STAGING)/usr/share/man/man1/mde.1.gz
	gzip -9 -c $(MAN_DIR)/md2epub.1 > $(DEB_STAGING)/usr/share/man/man1/md2epub.1.gz
	chmod 644 $(DEB_STAGING)/usr/share/man/man1/mde.1.gz
	chmod 644 $(DEB_STAGING)/usr/share/man/man1/md2epub.1.gz
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
	@echo "Depends: libc6, libncursesw6 (>= 6), libzip4 (>= 1.0)" >> $(DEB_STAGING)/DEBIAN/control
	@echo "Suggests: pandoc" >> $(DEB_STAGING)/DEBIAN/control
	@echo "Description: Full-screen text-mode Markdown editor with native EPUB exporter" >> $(DEB_STAGING)/DEBIAN/control
	@echo " mde is a specialized terminal text editor designed for editing and viewing" >> $(DEB_STAGING)/DEBIAN/control
	@echo " Markdown (.md) documents with dual-pane live preview, in-place rich" >> $(DEB_STAGING)/DEBIAN/control
	@echo " syntax styling, column ruler, task checkboxes, UTF-8 support, and native" >> $(DEB_STAGING)/DEBIAN/control
	@echo " EPUB 2/3 e-book export with the standalone md2epub tool." >> $(DEB_STAGING)/DEBIAN/control
	# Set standard Debian directory and file permissions
	find $(DEB_STAGING) -type d -exec chmod 755 {} +
	find $(DEB_STAGING) -type f -exec chmod 644 {} +
	chmod 755 $(DEB_STAGING)/usr/bin/mde
	chmod 755 $(DEB_STAGING)/usr/bin/md2epub
	# Build the Debian package
	dpkg-deb --build --root-owner-group $(DEB_STAGING) $(BIN_DIR)/$(DEB_PKG_NAME).deb
	rm -rf $(DEB_STAGING)
