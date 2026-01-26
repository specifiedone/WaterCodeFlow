"""
Memory Watcher
Native extension for fast mutation tracking
"""

from setuptools import setup, Extension
import platform
import sys

# -----------------------------
# Build Flags
# -----------------------------

extra_compile_args = []
extra_link_args = []

system = platform.system()

if system in ("Linux", "Darwin"):
    extra_compile_args = ["-O3", "-Wall", "-fPIC", "-pthread"]
    extra_link_args = ["-pthread"]

elif system == "Windows":
    extra_compile_args = ["/O2", "/W3"]
    print("⚠️  Windows support is experimental")

else:
    raise RuntimeError(f"Unsupported platform: {system}")

# Optional debug build:
#   python setup.py build_ext --inplace --debug
if "--debug" in sys.argv:
    extra_compile_args = ["-O0", "-g"]

# -----------------------------
# Extension
# -----------------------------

memwatch = Extension(
    name="memwatch",
    sources=["memwatch.c"],
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
)

# -----------------------------
# Setup
# -----------------------------

setup(
    name="memwatch",
    version="1.0",
    description="Fast mutation watcher for Python",
    ext_modules=[memwatch],
)
