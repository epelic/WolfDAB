#!/usr/bin/env bash
# setup.sh — Download and build third-party dependencies for dabtx.
#
# Run this once after cloning:
#   MSYSTEM=UCRT64 /c/msys64/usr/bin/bash.exe -lc './setup.sh'
#
# Or from a regular bash (Git Bash / MSYS2 UCRT64 shell):
#   ./setup.sh
#
# What it does:
#   1. Clones fdk-aac-dabplus (Opendigitalradio fork) if missing
#   2. Builds fdk-aac-dabplus as a static library
#   3. Installs to third_party/install/ (local prefix)
#
# Prerequisites: autoconf, automake, libtool, make, gcc (all from MSYS2)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FDK_DIR="$SCRIPT_DIR/third_party/fdk-aac-dabplus"
INSTALL_PREFIX="$SCRIPT_DIR/third_party/install"
FDK_REPO="https://github.com/Opendigitalradio/fdk-aac.git"

# Colors (if terminal supports them)
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; NC='\033[0m'

info()  { echo -e "${GREEN}[setup]${NC} $*"; }
warn()  { echo -e "${YELLOW}[setup]${NC} $*"; }
error() { echo -e "${RED}[setup]${NC} $*"; exit 1; }

# --- Step 1: fdk-aac-dabplus source ---

if [ -f "$FDK_DIR/configure.ac" ]; then
    info "fdk-aac-dabplus source found at $FDK_DIR"
else
    info "fdk-aac-dabplus not found — cloning from $FDK_REPO ..."
    git clone --depth 1 "$FDK_REPO" "$FDK_DIR" \
        || error "git clone failed. Check your internet connection."
    info "Clone complete."
fi

# --- Step 2: Check build tools ---

for tool in autoconf automake libtool make gcc; do
    if ! command -v "$tool" &>/dev/null; then
        error "'$tool' not found. Install MSYS2 packages:\n  pacman -S autoconf automake libtool make"
    fi
done

# --- Step 3: Build fdk-aac-dabplus ---

if [ -f "$INSTALL_PREFIX/lib/libfdk-aac.a" ]; then
    info "fdk-aac already built at $INSTALL_PREFIX/lib/libfdk-aac.a"
    info "To rebuild: rm -rf $INSTALL_PREFIX && ./setup.sh"
else
    info "Building fdk-aac-dabplus ..."
    cd "$FDK_DIR"

    if [ ! -f configure ]; then
        info "Running bootstrap ..."
        ./bootstrap || error "bootstrap failed"
    fi

    info "Configuring (prefix=$INSTALL_PREFIX) ..."
    ./configure --prefix="$INSTALL_PREFIX" \
                --enable-static --disable-shared \
                --silent \
        || error "configure failed"

    info "Compiling ..."
    make -j"$(nproc)" --silent \
        || error "make failed"

    info "Installing to $INSTALL_PREFIX ..."
    make install --silent \
        || error "make install failed"

    cd "$SCRIPT_DIR"
    info "fdk-aac-dabplus build complete."
fi

# --- Done ---

echo ""
info "Setup complete. Now build dabtx:"
info "  export PATH=\"/c/msys64/ucrt64/bin:/c/Program Files/CMake/bin:\$PATH\""
info "  export PKG_CONFIG_PATH=\"/c/msys64/ucrt64/lib/pkgconfig\""
info "  export CC=gcc CXX=g++"
info "  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release"
info "  cmake --build build"
info "  ctest --test-dir build"
