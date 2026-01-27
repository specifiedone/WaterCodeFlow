# Makefile for memwatch

.PHONY: all build install test clean examples help

# Default target
all: build

# Build the native extension
build:
	@echo "Building memwatch native extension..."
	python3 setup.py build_ext --inplace
	@echo "✓ Build complete"

# Install memwatch
install: build
	@echo "Installing memwatch..."
	python3 setup.py install
	@echo "✓ Installation complete"

# Install in development mode
develop: build
	@echo "Installing memwatch in development mode..."
	python3 setup.py develop
	@echo "✓ Development installation complete"

# Run all tests
test: build
	@echo "Running tests..."
	@echo ""
	@echo "=== Page Sharing Test ==="
	python3 tests/page_sharing_test.py
	@echo ""
	@echo "=== Overflow Test ==="
	python3 tests/overflow_test.py
	@echo ""
	@echo "✓ All tests complete"

# Run examples
examples: build
	@echo "Running examples..."
	@echo ""
	@echo "=== Small Buffer Demo ==="
	python3 examples/small_buffer_demo.py
	@echo ""
	@echo "=== Large Buffer Demo ==="
	python3 examples/large_buffer_demo.py
	@echo ""
	@echo "✓ All examples complete"

# Run demos only (shorter than full examples)
demo: build
	@echo "Running small buffer demo..."
	python3 examples/small_buffer_demo.py

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf build/
	rm -rf dist/
	rm -rf *.egg-info/
	rm -f src/*.o
	rm -f src/*.so
	rm -f memwatch*.so
	rm -f python/memwatch/*.pyc
	rm -rf python/memwatch/__pycache__/
	rm -rf examples/__pycache__/
	rm -rf tests/__pycache__/
	@echo "✓ Clean complete"

# Display statistics
stats: build
	@echo "Analyzing memwatch code statistics..."
	@echo ""
	@echo "=== Source Files ==="
	@find src python examples tests -name "*.c" -o -name "*.py" | wc -l | xargs echo "  Files:"
	@cat src/*.c python/memwatch/*.py | wc -l | xargs echo "  Lines of code:"
	@echo ""
	@echo "=== Native Core (C) ==="
	@wc -l src/memwatch.c | awk '{print "  " $$1 " lines"}'
	@grep -E "^(typedef|struct|static|void|int|uint)" src/memwatch.c | wc -l | xargs echo "  Functions/structs:"
	@echo ""
	@echo "=== Python API ==="
	@wc -l python/memwatch/__init__.py | awk '{print "  " $$1 " lines"}'
	@wc -l python/memwatch/adapters.py | awk '{print "  " $$1 " lines (adapters)"}'
	@echo ""
	@echo "=== Documentation ==="
	@wc -l python/memwatch/architecture.md | awk '{print "  " $$1 " lines"}'
	@wc -l README.md | awk '{print "  " $$1 " lines (README)"}'

# Check code for common issues
check: build
	@echo "Checking code..."
	@echo "✓ Build successful (no compilation errors)"
	@echo "✓ Python syntax valid"
	@python3 -m py_compile python/memwatch/__init__.py
	@python3 -m py_compile python/memwatch/adapters.py
	@echo "✓ Examples syntax valid"
	@python3 -m py_compile examples/*.py
	@python3 -m py_compile tests/*.py
	@echo "✓ All checks passed"

# Run with valgrind (memory leak detection)
valgrind: build
	@echo "Running with valgrind (memory leak detection)..."
	valgrind --leak-check=full --show-leak-kinds=all \
		python3 examples/small_buffer_demo.py

# Quick test (fastest verification)
quick: build
	@echo "Quick test..."
	python3 -c "from memwatch import MemoryWatcher; w = MemoryWatcher(); b = bytearray(100); w.watch(b); print('✓ memwatch working')"

# Display help
help:
	@echo "memwatch Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make              - Build native extension"
	@echo "  make build        - Build native extension"
	@echo "  make install      - Build and install"
	@echo "  make develop      - Install in development mode"
	@echo "  make test         - Run all tests"
	@echo "  make examples     - Run all examples"
	@echo "  make demo         - Run quick demo"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make stats        - Show code statistics"
	@echo "  make check        - Check for common issues"
	@echo "  make valgrind     - Run with memory leak detection"
	@echo "  make quick        - Quick verification test"
	@echo "  make help         - Show this help"
