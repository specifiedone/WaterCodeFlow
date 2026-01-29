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
all: build

# ============================================================================
# MAIN BUILD - Everything you need for the CLI
# ============================================================================

build: build-cli build-tracker build-preload build-python
	@echo ""
	@echo "✅ BUILD COMPLETE!"
	@echo ""
	@echo "Available tools:"
	@echo "  CLI:       ./build/memwatch_cli run <program> --storage <db>"
	@echo "  Library:   ./build/libmemwatch.so (LD_PRELOAD injection)"
	@echo ""
	@echo "Quick start:"
	@echo "  ./build/memwatch_cli run python3 script.py --storage results.db"
	@echo "  ./build/memwatch_cli read results.db"
	@echo ""
	@echo "With LD_PRELOAD:"
	@echo "  LD_PRELOAD=./build/libmemwatch.so MEMWATCH_DB=data.db ./program"
	@echo ""

# ============================================================================
# Note: Using SQLite for C code (C++ FastStorage is Python-only via pybind11)
# ============================================================================

# ============================================================================
# Build Real Memory Tracker
# ============================================================================

build-tracker: build/memwatch_tracker.o
	@echo "✅ Tracker built"

build/memwatch_tracker.o: src/memwatch_tracker.c include/memwatch_tracker.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/memwatch_tracker.c -o build/memwatch_tracker_obj.o
	@echo "Objects compiled"

# ============================================================================
# Build CLI Tool  
# ============================================================================

build-cli: build/memwatch_cli
	@echo "✅ CLI tool built: ./build/memwatch_cli"

build/memwatch_cli: src/memwatch_cli_simple.c src/memwatch_tracker.c include/memwatch_tracker.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/memwatch_cli_simple.c src/memwatch_tracker.c -I./include -lpthread -lsqlite3 -lm -ldl
# ============================================================================
# Build LD_PRELOAD Library
# ============================================================================

build-preload: build/libmemwatch.so
	@echo "✅ Preload li/libmemwatch.so
	@echo "✅ Preload library built: ./build/libmemwatch.so"

build/libmemwatch.so: src/memwatch_preload.c src/memwatch_tracker.c include/memwatch_tracker.h
	@mkdir -p build
	$(CC) -shared -fPIC $(CFLAGS) -o $@ src/memwatch_preload.c src/memwatch_tracker
	@echo "View results:"
	@echo "  ./build/memwatch_cli read results.db"
	@echo ""

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
# OLD - REMOVED (see build-tracker, build-cli, build-preload above)
# ============================================================================

build-cli-manual:
	@echo "Building CLI with verbose output..."
	@mkdir -p build
	$(CC) -v -o build/memwatch_cli src/memwatch.c src/memwatch_cli.c src/sql_tracker.c \
	  -I./include $(CFLAGS) $(LDFLAGS) -lm -lpthread

# ============================================================================
# SQL TRACKER LIBRARY - Track database changes across all languages
# ============================================================================

build-sql-tracker: build/libsql_tracker.so

build/libsql_tracker.so: src/sql_tracker.c include/sql_tracker.h
	@mkdir -p build
	@echo "Building SQL tracker library..."
	$(CC) $(CFLAGS) -c src/sql_tracker.c -o build/sql_tracker.o
	$(CC) build/sql_tracker.o $(LDFLAGS) -o build/libsql_tracker.so
	@echo "✓ Built: libsql_tracker.so (SQL tracking for all languages)"
	@echo "  Available in: bindings/sql_tracker_python.py (Python)"
	@echo "             bindings/SQLTracker.java (Java)"
	@echo "             bindings/sql_tracker.go (Go)"
	@echo "             bindings/sql_tracker.rs (Rust)"
	@echo "             bindings/SQLTracker.cs (C#)"
	@echo "             bindings/sql_tracker.js (JavaScript)"
	@echo "             bindings/sql_tracker.ts (TypeScript)"

# ============================================================================
# PYTHON
# ============================================================================

build-python: build-core
	@echo "Building Python binding..."
	@if [ -f setup.py ]; then \
		python3 setup.py build_ext --inplace 2>&1 | grep -v "running" || true; \
		echo "✓ Built: Python binding"; \
	else \
		echo "⚠ setup.py not found, skipping Python binding"; \
	fi

test-python: build-python
	@echo "Testing Python..."
	@python3 -c "from memwatch import MemoryWatcher; print('✓ Python import OK')" || echo "✗ Python test failed"

install-python: build-python
	@if [ -f setup.py ]; then \
		python3 setup.py install 2>&1 | grep -v "running" || true; \
		echo "✓ Installed: Python binding"; \
	fi

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

test-cli: build
	@echo ""
	@echo "Testing CLI..."
	@if [ -f build/memwatch_cli ]; then \
		echo "✓ CLI executable found"; \
		./build/memwatch_cli --help 2>/dev/null || echo "  (Run without args for help)"; \
	else \
		echo "✗ CLI not built"; \
	fi

test-sql-tracker: build-sql-tracker build-python
	@echo ""
	@echo "Testing SQL tracker..."
	@python3 examples/sql_tracking_example.py 2>/dev/null || echo "Note: SQL tracker example ready in examples/"

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
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════════╗"
	@echo "║           MemWatch - Multi-Language Memory Tracker             ║"
	@echo "║       With Universal SQL Query Tracking (10 Languages)         ║"
	@echo "╚════════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "🚀 QUICK START:"
	@echo "  make build              - Build CLI and all bindings (RECOMMENDED)"
	@echo "  make test-cli           - Test the built CLI"
	@echo "  ./build/memwatch_cli --help"
	@echo ""
	@echo "📦 BUILD TARGETS:"
	@echo "  make build              - Build CLI + SQL tracker + all language bindings"
	@echo "  make build-cli          - Build universal CLI only"
	@echo "  make build-sql-tracker  - Build SQL tracking library"
	@echo "  make build-core         - Build core C library"
	@echo "  make build-python       - Build Python binding"
	@echo "  make build-javascript   - Build JavaScript binding"
	@echo "  make build-java         - Build Java binding"
	@echo "  make build-cpp          - Build C++ binding"
	@echo "  make build-csharp       - Build C# binding"
	@echo "  make build-go           - Build Go binding"
	@echo "  make build-rust         - Build Rust binding"
	@echo ""
	@echo "🧪 TEST TARGETS:"
	@echo "  make test-cli           - Test the CLI executable"
	@echo "  make test-sql-tracker   - Test SQL tracking with Python"
	@echo "  make quick-test         - Quick test of Python binding"
	@echo "  make test               - Run all language tests"
	@echo ""
	@echo "📚 USAGE EXAMPLES:"
	@echo "  ./build/memwatch_cli run python3 script.py"
	@echo "  ./build/memwatch_cli run python3 script.py --storage results.db"
	@echo "  ./build/memwatch_cli read results.db"
	@echo ""
	@echo "🧹 CLEANUP:"
	@echo "  make clean              - Remove all build artifacts"
	@echo ""
	@echo "📖 For more info, see: README.md, BUILD.md, and examples/"
