#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")"

config="${1:-debug}"

# Prefer a premake5 on PATH; vendor/premake5 is a Linux x86-64 binary.
if command -v premake5 >/dev/null 2>&1; then
    premake=premake5
else
    premake=vendor/premake5
fi

"$premake" gmake
make clean
make config="$config" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" all
