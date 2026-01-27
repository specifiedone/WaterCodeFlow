#!/usr/bin/env python3
"""
setup.py for memwatch

Builds the native C extension module for memory change watching.
"""

from setuptools import setup, Extension
import sys

# Native extension module
memwatch_extension = Extension(
    'memwatch',
    sources=['src/memwatch.c'],
    include_dirs=['/usr/include', '/usr/local/include'],
    libraries=['pthread'],
    extra_compile_args=[
        '-std=c11',
        '-O3',
        '-Wall',
        '-Wextra',
        '-fPIC',
        '-D_GNU_SOURCE',
    ],
    extra_link_args=['-lpthread']
)

# Platform-specific settings
if sys.platform == 'darwin':
    # macOS
    memwatch_extension.extra_compile_args.append('-D_DARWIN_C_SOURCE')
elif sys.platform.startswith('linux'):
    # Linux
    memwatch_extension.extra_compile_args.append('-D_POSIX_C_SOURCE=200809L')
    memwatch_extension.libraries.append('rt')  # For clock_gettime
elif sys.platform == 'win32':
    # Windows - no mprotect, will use polling fallback
    memwatch_extension.define_macros = [('NO_MPROTECT', '1')]
    memwatch_extension.libraries = []

setup(
    name='memwatch',
    version='0.1.0',
    description='Language-agnostic memory change watcher',
    long_description=open('python/memwatch/architecture.md').read(),
    long_description_content_type='text/markdown',
    author='memwatch contributors',
    url='https://github.com/yourusername/memwatch',
    
    # Python package
    packages=['memwatch'],
    package_dir={'memwatch': 'python/memwatch'},
    
    # Native extension
    ext_modules=[memwatch_extension],
    
    # Requirements
    python_requires='>=3.7',
    install_requires=[
        # No external dependencies for core
        # Optional: 'numpy>=1.19.0' for array support
    ],
    extras_require={
        'dev': [
            'pytest>=6.0',
            'numpy>=1.19.0',
        ],
        'storage': [
            # If using FastStorage
            # 'storage_utility>=1.0.0',
        ]
    },
    
    # Metadata
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'Topic :: Software Development :: Debugging',
        'Topic :: System :: Monitoring',
        'License :: OSI Approved :: MIT License',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: 3.11',
        'Programming Language :: C',
    ],
    
    # Entry points
    entry_points={
        'console_scripts': [
            'memwatch-demo=examples.small_buffer_demo:main',
        ],
    },
)
