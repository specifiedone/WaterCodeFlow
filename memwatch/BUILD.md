# Build System Documentation

## Overview

MemWatch uses a sophisticated Makefile-based build system that compiles:
1. **Core memory tracking library** (C)
2. **Universal SQL tracking library** (C)
3. **Language bindings** (8 languages)
4. **Command-line interface** (universal CLI)

## Quick Commands

| Command | What it does |
|---------|--------------|
| `make build` | Build CLI + SQL tracker + Python binding (RECOMMENDED) |
| `make clean` | Remove all build artifacts |
| `make test-cli` | Test the built CLI |
| `make help` | Show all available targets |

## Build Architecture

### Main Build Target: `make build`

When you run `make build`, it orchestrates:

1. **build-core** - Compiles C core memory tracking
   - Input: `src/memwatch.c`
   - Output: `build/libmemwatch_core.so` (27 KB)
   - Time: ~1 second

2. **build-sql-tracker** - Compiles SQL tracking library
   - Input: `src/sql_tracker.c`
   - Output: `build/libsql_tracker.so` (21 KB)
   - Provides API for all 10 languages
   - Time: ~1 second

3. **build-cli** - Builds universal command-line interface
   - Input: `src/memwatch_cli.c` + core libraries
   - Output: `build/memwatch_cli` (27 KB executable)
   - Works with any program in any language
   - Time: ~2 seconds

4. **build-python** - Builds Python native extension
   - Input: `src/memwatch.c` + `setup.py`
   - Output: `build/lib.linux-x86_64-python3.12/_memwatch_native*.so`
   - Used by Python scripts
   - Time: ~3 seconds

**Total build time: ~7 seconds**

## Build Output

After `make build`, the build directory contains:

```
build/
├── memwatch_cli                    # Main executable (27 KB)
├── libmemwatch_core.so             # Core library
├── libsql_tracker.so               # SQL tracking library
├── _memwatch_native.cpython-*.so   # Python binding
├── memwatch.o                      # Object files (intermediate)
├── memwatch_cli.o                  #
└── sql_tracker.o                   #
```

## Language-Specific Builds

If you want to build support for a specific language:

```bash
# Build individual language bindings
make build-python       # Python: native extension
make build-javascript   # JavaScript: Node.js module
make build-java         # Java: requires Java compiler
make build-cpp          # C++: header + linking
make build-csharp       # C#: requires mono/dotnet
make build-go           # Go: requires Go toolchain
make build-rust         # Rust: requires Cargo
```

## Available Bindings

All language bindings are in `bindings/` directory:

| Language | File | Status |
|----------|------|--------|
| Python | `sql_tracker_python.py` | ✅ Production |
| Java | `SQLTracker.java` | ✅ Production |
| JavaScript | `sql_tracker.js` | ✅ Production |
| TypeScript | `sql_tracker.ts` | ✅ Production |
| C# | `SQLTracker.cs` | ✅ Production |
| Go | `sql_tracker.go` | ✅ Production |
| Rust | `sql_tracker.rs` | ✅ Production |
| C | `sql_tracker_c.h` | ✅ Production |

## Using After Build

### CLI Interface

```bash
# Track a program and save to database
./build/memwatch_cli run python3 script.py --storage results.db

# View results
./build/memwatch_cli read results.db

# With options
./build/memwatch_cli run java -jar App.jar \
  --storage tracking.db \
  --scope both \
  --threads
```

### Python Module

```python
# Use Python binding
from memwatch import MemoryWatcher

watcher = MemoryWatcher()
watcher.start_tracking()
# Code to track...
results = watcher.stop_tracking()
print(results)
```

### C/C++ Linking

```c
#include "memwatch_unified.h"

// Use memwatch_core or libmemwatch_core.so
memwatch_init();
memwatch_track_variable("myvar", ptr, size);
// ...
```

## Compiler Configuration

The build uses these defaults:

```makefile
CC = gcc
CFLAGS = -fPIC -Wall -O2 -I./include
LDFLAGS = -shared -lm -lpthread -ldl
```

To override:

```bash
# Use custom compiler
CC=clang make build

# Custom flags
CFLAGS="-g -O0" make build
```

## Build Targets Reference

### Primary Targets

| Target | Purpose | Depends On |
|--------|---------|-----------|
| `make build` | Complete build | build-cli, build-sql-tracker, build-python |
| `make build-core` | Memory tracking | gcc, src/memwatch.c |
| `make build-cli` | CLI tool | build-core, build-sql-tracker |
| `make build-sql-tracker` | SQL tracking | gcc, src/sql_tracker.c |
| `make build-python` | Python binding | build-core, setup.py |

### Test Targets

| Target | Purpose |
|--------|---------|
| `make test-cli` | Test CLI compilation |
| `make test-sql-tracker` | Test SQL tracker |
| `make quick-test` | Quick Python test |
| `make test` | All language tests |

### Maintenance Targets

| Target | Purpose |
|--------|---------|
| `make clean` | Remove build artifacts |
| `make install` | Install Python binding |
| `make install-all` | Install all bindings |
| `make help` | Show build help |

## Troubleshooting Build Issues

### Python binding fails

```bash
# Make sure Python development headers are available
python3 -m pip install build

# Try manual build
cd . && python3 setup.py build_ext --inplace
```

### gcc not found

```bash
# Install build essentials
sudo apt-get install build-essential python3-dev

# Or use clang
CC=clang make build
```

### SQL tracker warnings

The build may show compiler warnings about uninitialized variables in sql_tracker.c. These are safe to ignore (runtime initialization paths are correct).

To suppress:

```bash
CFLAGS="-fPIC -Wall -O2 -Wno-maybe-uninitialized" make build
```

### Permission denied on CLI

After building, the CLI should be executable:

```bash
chmod +x build/memwatch_cli
./build/memwatch_cli --help
```

## Advanced: Custom Build

To customize the build:

1. **Edit compiler flags in Makefile:**
   ```makefile
   CFLAGS = -fPIC -Wall -O3 -march=native
   ```

2. **Add new source files:**
   ```makefile
   build-core: src/memwatch.c src/myfile.c
   	gcc $(CFLAGS) -c src/memwatch.c src/myfile.c ...
   ```

3. **Add new language binding:**
   - Create binding file in `bindings/`
   - Add build target to Makefile
   - Update documentation

## Environment Variables

These can be set to customize the build:

```bash
CC              # C compiler (default: gcc)
CFLAGS          # Compiler flags
LDFLAGS         # Linker flags
DEBUG           # Build with debug symbols (set to 1)
```

Example:

```bash
DEBUG=1 CC=clang make build
```

## CI/CD Integration

The build system is CI/CD friendly:

```bash
# GitHub Actions / GitLab CI / Jenkins
make clean
make build
make test-cli
```

Exit codes are standard (0 = success, 1 = failure).

## Performance

Build times on modern hardware:

- **make build** (all): ~7 seconds
- **make build-cli** (CLI only): ~2 seconds  
- **make build-python** (Python only): ~3 seconds
- **make clean**: ~1 second

## Next Steps

1. Run `make build` to build everything
2. Run `make test-cli` to verify
3. Run `./build/memwatch_cli` to see available commands
4. Check [QUICK_START.md](QUICK_START.md) for usage examples

## Get Help

```bash
make help       # Show all available targets
```

See [README.md](README.md) for complete documentation.
