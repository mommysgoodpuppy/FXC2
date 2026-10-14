#!/usr/bin/env bash
# Build the helper regression test; Wine is needed only to execute this test.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
: "${FXC2_TOOLCHAIN:?Source out/unity-linux-env.sh or set FXC2_TOOLCHAIN}"
WORK="${FXC2_UNITY_WORK:-$REPO/out/unity-linux}"
WORK="$(cd "$WORK" && pwd)"
if [ ! -f "$WORK/unity_float.o" ]; then
    echo "Build the Unity DLL first to produce unity_float.o." >&2
    exit 1
fi
"$FXC2_TOOLCHAIN/bin/x86_64-w64-mingw32-clang" -O2 -g0 -fno-builtin \
    -I"$REPO/src" -include "$REPO/src/unity_runtime.h" \
    "$REPO/tests/unity_runtime_test.c" "$REPO/src/unity_runtime.c" \
    "$WORK/unity_float.o" -o "$WORK/runtime-test.exe"
# Never use or modify the user's default Wine prefix.
WINEPREFIX="$WORK/wine-runtime-test" WINEDEBUG=-all wine "$WORK/runtime-test.exe"
