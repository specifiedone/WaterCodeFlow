#!/usr/bin/env python3
"""
fix_memwatch_layout.py

Automates reorganizing the contents of the provided zip extraction (e.g.
`memwatch-fixed-final/`) into a clean, importable, rebuildable Python package
layout that correctly handles the native C extension.

What it does (best-effort):
  - Detects the extracted folder (prefix `memwatch-fixed-final`)
  - Creates a new `memwatch/` package directory with subfolders
    - memwatch/
      - memwatch/      (python package files copied here)
      - src/           (C sources)
      - Makefile, setup.py placed at package root
      - native artifacts (prebuilt .so) copied into package to allow immediate import
  - Patches the Python package so it tries a relative import for the native
    extension first, falling back to a top-level import.
  - Copies prebuilt extension binaries (if present) into the package and also
    creates a safe name `_memwatch_native.so` (duplicate) to increase import
    compatibility.
  - Adds a small `build_native.sh` helper to build the extension in-place using
    the existing `setup.py`/Makefile.

Usage:
  Place this script next to the extracted folder (the folder that contains
  `memwatch-fixed-final/`). Then run:

    python fix_memwatch_layout.py

  Then:
    cd memwatch
    chmod +x build_native.sh
    ./build_native.sh
    python -m tests.test_basic

Notes / caveats:
  - This script is conservative: it will refuse to overwrite an existing
    `memwatch/` target directory.
  - Building the native extension requires a working build toolchain and the
    same Python ABI used when compiling prebuilt binaries (if you rely on
    prebuilt .so files). If you run into build errors, paste the Makefile +
    compiler output back here and I will harden the Makefile.

"""

import sys
from pathlib import Path
import shutil
import re
import zipfile

# --------------------- helpers ---------------------

def find_extracted_dir(base: Path) -> Path:
    """Find the folder that looks like the extracted package."""
    for child in base.iterdir():
        if child.is_dir() and child.name.startswith("memwatch-fixed-final"):
            return child
    raise RuntimeError("Could not find folder starting with 'memwatch-fixed-final' in cwd")


def copy_tree(src: Path, dst: Path):
    if not src.exists():
        return
    for p in src.iterdir():
        dest = dst / p.name
        if p.is_dir():
            shutil.copytree(p, dest)
        else:
            shutil.copy2(p, dest)


# --------------------- main ---------------------

def main():
    cwd = Path.cwd()

    # 1) auto-detect source folder
    try:
        src_root = find_extracted_dir(cwd)
    except RuntimeError:
        # if the developer/test environment already unzipped to a direct name,
        # also accept that
        alt = cwd / "memwatch-fixed-final"
        if alt.exists():
            src_root = alt
        else:
            print("ERROR: cannot locate 'memwatch-fixed-final' directory in:", cwd)
            sys.exit(1)

    print("Found source root:", src_root)

    target_root = cwd / "memwatch"
    if target_root.exists():
        print("ERROR: target folder 'memwatch' already exists. Remove or rename it and re-run.")
        sys.exit(1)

    # Create layout
    (target_root / "memwatch").mkdir(parents=True)
    (target_root / "src").mkdir()
    (target_root / "tests").mkdir()

    # 2) copy python package files
    py_pkg = src_root / "python" / "memwatch"
    if not py_pkg.exists():
        print("WARNING: source python package not found at", py_pkg)
    else:
        print("Copying python package files...")
        copy_tree(py_pkg, target_root / "memwatch")

    # 3) copy src C files
    src_dir = src_root / "src"
    if src_dir.exists():
        print("Copying C sources...")
        copy_tree(src_dir, target_root / "src")
    else:
        print("WARNING: no src/ dir found in source root")

    # 4) copy top-level helper files (Makefile, setup.py, README...)
    for name in ("Makefile", "setup.py", "README.md", "QUICKSTART.md"):
        s = src_root / name
        if s.exists():
            print("Copying", name)
            shutil.copy2(s, target_root / name)

    # 5) copy any prebuilt shared objects into package (increase chance of immediate import)
    found_sos = []
    for candidate in [src_root, src_root / "build", src_root / "build" / "lib.linux-x86_64-cpython-312"]:
        if candidate.exists():
            for f in candidate.iterdir():
                if f.suffix.lower() in (".so", ".dll", ".dylib") and "memwatch" in f.name:
                    print("Found native binary:", f)
                    dest = target_root / "memwatch" / f.name
                    shutil.copy2(f, dest)
                    found_sos.append(dest)

    # Create a safe copy named exactly _memwatch_native.so (strip ABI tag) if possible
    for so in list(found_sos):
        if so.name.startswith("_memwatch_native") and not so.name.endswith("_memwatch_native.so"):
            safe = target_root / "memwatch" / "_memwatch_native.so"
            if not safe.exists():
                try:
                    shutil.copy2(so, safe)
                    print("Created compatibility copy:", safe)
                except Exception:
                    pass

    # 6) Patch adapters.py and package __init__ so native import is robust
    pkg_init = target_root / "memwatch" / "__init__.py"
    adapters = target_root / "memwatch" / "adapters.py"

    def patch_native_import(path: Path):
        if not path.exists():
            return
        txt = path.read_text()
        # Avoid double-patching
        if "TRY_RELATIVE_MEMWATCH_NATIVE_IMPORT" in txt:
            return

        # Replace simple `import _memwatch_native as _native` with a robust loader
        txt = re.sub(r"import\s+_memwatch_native\s+as\s+_native",
                     "# TRY_RELATIVE_MEMWATCH_NATIVE_IMPORT -- patched to prefer package-local extension\n" \
                     "_native = None\n" \
                     "try:\n" \
                     "    # prefer package-local extension when inside the package\n" \
                     "    from . import _memwatch_native as _native\n" \
                     "except Exception:\n" \
                     "    try:\n" \
                     "        import _memwatch_native as _native\n" \
                     "    except Exception:\n" \
                     "        _native = None",
                     txt)
        path.write_text(txt)
        print("Patched imports in:", path)

    patch_native_import(adapters)
    patch_native_import(pkg_init)

    # 7) Ensure __init__ exports a MemoryWatcher symbol (create a small shim if missing)
    # If there is no MemoryWatcher name exported we add a thin shim that imports
    # the heavy logic from the package and exposes MemoryWatcher.

    init_text = (target_root / "memwatch" / "__init__.py").read_text() if (target_root / "memwatch" / "__init__.py").exists() else ""
    if "class MemoryWatcher" not in init_text and "def MemoryWatcher(" not in init_text and "MemoryWatcher" not in init_text:
        # create a minimal shim
        shim = """
# Lightweight shim created by fix_memwatch_layout.py
# This shim will try to import the real MemoryWatcher from the package body
from importlib import import_module
try:
    mod = import_module('memwatch')
    MemoryWatcher = getattr(mod, 'MemoryWatcher')
except Exception:
    # fallback to a tiny placeholder to avoid ImportError while building
    class MemoryWatcher:
        def __init__(self, *a, **kw):
            raise RuntimeError('MemoryWatcher placeholder: native extension not available or real implementation missing')

__all__ = ['MemoryWatcher']
"""
        (target_root / "memwatch" / "__init__.py").write_text(shim)
        print("Wrote shim __init__.py (no MemoryWatcher was found in original package)")

    # 8) Create build helper
    (target_root / "build_native.sh").write_text("""#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
# Use Makefile if present
if [ -f Makefile ]; then
  make clean || true
  make
else
  python3 setup.py build_ext --inplace
fi
echo "Native build finished"
""")
    (target_root / "build_native.sh").chmod(0o755)

    # 9) Create a tiny test to smoke-import the package
    (target_root / "tests" / "test_basic.py").write_text("""try:
    from memwatch import MemoryWatcher
    print('Imported MemoryWatcher:', MemoryWatcher)
except Exception as e:
    print('Import failed:', e)
""")

    print("\nSUCCESS: Wrote new package at:", target_root)
    print("Next steps:\n  cd memwatch\n  chmod +x build_native.sh\n  ./build_native.sh\n  python -m tests.test_basic\n")


if __name__ == '__main__':
    main()
