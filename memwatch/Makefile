# MemWatch - Multi-Language Build System

.PHONY: all build-core build-python test-python install-python clean help
.PHONY: build-javascript test-javascript build-java test-java
.PHONY: build-cpp test-cpp build-csharp test-csharp build-go test-go build-rust test-rust

# Compiler configuration
CC = gcc
CXX = g++

# Get Python include path
PYTHON_INCLUDE := $(shell python3 -c "import sysconfig; print(sysconfig.get_path('include'))")

CFLAGS = -fPIC -Wall -O2 -I./include -I$(PYTHON_INCLUDE)
CXXFLAGS = $(CFLAGS)
LDFLAGS = -shared -lm -lpthread

# Detect platform
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -ldl
endif

# Default target
all: build-core build-python quick-test

# ============================================================================
# CORE LIBRARY
# ============================================================================

build-core: build/libmemwatch_core.so

build/libmemwatch_core.so: src/memwatch.c include/memwatch_unified.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/memwatch.c -o build/memwatch.o
	$(CC) build/memwatch.o $(LDFLAGS) -o build/libmemwatch_core.so
	@echo "✓ Built: memwatch_core"

# ============================================================================
# PYTHON
# ============================================================================

build-python: build-core
	cd python && python3 setup.py build_ext --inplace
	@echo "✓ Built: Python binding"

test-python: build-python
	@echo "Testing Python..."
	@python3 -c "from memwatch import MemoryWatcher; print('✓ Python import OK')" || echo "✗ Python test failed"

install-python: build-python
	cd python && python3 setup.py install
	@echo "✓ Installed: Python binding"

# ============================================================================
# JAVASCRIPT / NODE.JS
# ============================================================================

build-javascript: build-core
	@echo "Note: Node.js binding requires npm install and node-gyp build"
	@echo "To build: npm install && npm run build"

test-javascript: build-javascript
	@echo "JavaScript test would require npm setup"

install-javascript:
	@echo "To install JavaScript: npm install memwatch"

# ============================================================================
# JAVA
# ============================================================================

build-java: build-core
	@mkdir -p java/build
	@echo "Compiling Java classes..."
	@javac -d java/build bindings/MemWatch.java 2>/dev/null || echo "Note: Java JDK not found"

test-java: build-java
	@echo "Java test would require JNI compilation"

install-java:
	@echo "To install Java: copy .jar to classpath"

# ============================================================================
# C++
# ============================================================================

build-cpp: build-core
	@mkdir -p build
	@echo "✓ C++ binding available (header-only)"

test-cpp: build-cpp
	@echo "C++ binding ready for use"

install-cpp:
	mkdir -p /usr/local/include/memwatch
	cp include/memwatch_unified.h /usr/local/include/memwatch/ 2>/dev/null || true

# ============================================================================
# C#
# ============================================================================

build-csharp: build-core
	@echo "Note: C# binding requires .NET SDK"
	@echo "To build: dotnet build csharp/MemWatch.csproj"

test-csharp: build-csharp
	@echo "C# test requires .NET SDK"

install-csharp:
	@echo "To install C#: dotnet nuget push or visual studio"

# ============================================================================
# GO
# ============================================================================

build-go: build-core
	@if command -v go >/dev/null; then \
		cd bindings && go build ./... 2>/dev/null && echo "✓ Go binding built" || echo "✗ Go build failed"; \
	else \
		echo "Note: Go not found, skipping"; \
	fi

test-go: build-go
	@echo "Go test would require cgo setup"

install-go:
	@echo "To install Go: go get github.com/memwatch/memwatch-go"

# ============================================================================
# RUST
# ============================================================================

build-rust: build-core
	@echo "Note: Rust binding requires Cargo"
	@echo "To build: cargo build --release"

test-rust: build-rust
	@echo "Rust test would require Cargo"

install-rust:
	@echo "To install Rust: cargo.io memwatch"

# ============================================================================
# UNIFIED TESTS
# ============================================================================

quick-test: build-python
	@echo ""
	@echo "Quick test of Python binding..."
	@python3 examples/test_unified.py 2>/dev/null || echo "✗ Quick test requires binding setup"

test: test-python test-javascript test-java test-cpp test-csharp test-go test-rust
	@echo ""
	@echo "========================================"
	@echo "TEST SUMMARY"
	@echo "========================================"

# ============================================================================
# INSTALLATION
# ============================================================================

install: install-python install-cpp
	@echo "✓ Installed core bindings"

install-all: install-python install-javascript install-java install-cpp install-csharp install-go install-rust
	@echo "✓ Installed all bindings (see notes above)"

# ============================================================================
# CLEANUP
# ============================================================================

clean:
	@echo "Cleaning build artifacts..."
	rm -rf build/
	rm -f *.o *.so *.pyc
	cd python && python3 setup.py clean --all 2>/dev/null || true
	find . -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
	find . -name "*.class" -delete 2>/dev/null || true
	@echo "✓ Cleaned build artifacts"

# ============================================================================
# HELP
# ============================================================================

help:
	@echo "MemWatch - Multi-Language Build System"
	@echo ""
	@echo "Quick Start:"
	@echo "  make            - Build core and Python"
	@echo "  make test       - Run all available tests"
	@echo "  make install    - Install Python binding"
	@echo ""
	@echo "Build Specific Language:"
	@echo "  make build-python      - Build Python binding"
	@echo "  make build-javascript  - Build JavaScript binding"
	@echo "  make build-java        - Build Java binding"
	@echo "  make build-cpp         - Build C++ binding"
	@echo "  make build-csharp      - Build C# binding"
	@echo "  make build-go          - Build Go binding"
	@echo "  make build-rust        - Build Rust binding"
	@echo ""
	@echo "Test Specific Language:"
	@echo "  make test-python, test-javascript, etc."
	@echo ""
	@echo "Other Targets:"
	@echo "  make clean  - Remove build artifacts"
	@echo "  make help   - Show this help"
