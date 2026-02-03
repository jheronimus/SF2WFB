# Makefile for SF2WFB - SoundFont 2 to WaveFront Bank Converter
#
# Compatible with gcc and clang on macOS and Linux

# Compiler selection (defaults to cc, which is usually gcc or clang)
CC ?= cc

# Compiler flags
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = -lm

# Directories
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = .

# Target binaries
TARGET = $(BIN_DIR)/sf2wfb
DEBUG_TARGET = $(BIN_DIR)/sf2_debug

# Source files (exclude sf2_debug.c from main build)
SOURCES = $(filter-out $(SRC_DIR)/sf2_debug.c, $(wildcard $(SRC_DIR)/*.c))
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
HEADERS = $(wildcard $(INC_DIR)/*.h)

# Debug utility source files (exclude main.c)
DEBUG_SOURCES = $(filter-out $(SRC_DIR)/main.c, $(wildcard $(SRC_DIR)/*.c))
DEBUG_OBJECTS = $(DEBUG_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Default target
.PHONY: all
all: $(TARGET)

# Create object directory if it doesn't exist
$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

# Compile source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(OBJ_DIR)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Link object files to create the final binary
$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	@$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@echo "Build complete: $(TARGET)"

# Build the debug utility
.PHONY: debug
debug: $(DEBUG_TARGET)

$(DEBUG_TARGET): $(DEBUG_OBJECTS)
	@echo "Linking $@..."
	@$(CC) $(DEBUG_OBJECTS) $(LDFLAGS) -o $@
	@echo "Build complete: $(DEBUG_TARGET)"

# Clean build artifacts
.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(OBJ_DIR) $(TARGET) $(DEBUG_TARGET)
	@echo "Clean complete."

# Install target (optional - installs to /usr/local/bin)
.PHONY: install
install: $(TARGET)
	@echo "Installing $(TARGET) to /usr/local/bin..."
	@install -m 755 $(TARGET) /usr/local/bin/
	@echo "Installation complete."

# Uninstall target
.PHONY: uninstall
uninstall:
	@echo "Uninstalling sf2wfb from /usr/local/bin..."
	@rm -f /usr/local/bin/sf2wfb
	@echo "Uninstall complete."

# Help target
.PHONY: help
help:
	@echo "SF2WFB Makefile targets:"
	@echo "  all        - Build the sf2wfb binary (default)"
	@echo "  debug      - Build the sf2_debug utility"
	@echo "  clean      - Remove all build artifacts"
	@echo "  install    - Install to /usr/local/bin (requires sudo)"
	@echo "  uninstall  - Remove from /usr/local/bin (requires sudo)"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  CC         - C compiler (default: cc)"
	@echo "  CFLAGS     - Compiler flags"
	@echo "  LDFLAGS    - Linker flags"
	@echo ""
	@echo "Example usage:"
	@echo "  make              # Build with default compiler"
	@echo "  make debug        # Build debug utility"
	@echo "  make CC=gcc       # Build with gcc"
	@echo "  make CC=clang     # Build with clang"
	@echo "  sudo make install # Install the binary"

# Phony targets
.PHONY: all clean install uninstall help
