#!/usr/bin/env bash
# Experimental UCRT DLL for Linux Unity's PE loader. Tested shader builds;
# execution and complete runtime coverage still require validation.
# Host: autotools, flex, bison, Perl JSON, widl, SPIR-V + Vulkan headers.
# FXC2_TOOLCHAIN must name an unpacked LLVM-MinGW UCRT toolchain.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${FXC2_UNITY_WORK:-$REPO/out/unity-linux}"
: "${FXC2_TOOLCHAIN:?Set FXC2_TOOLCHAIN to an LLVM-MinGW UCRT directory}"
FXC2_TOOLCHAIN="$(cd "$FXC2_TOOLCHAIN" && pwd)"
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
export PATH="$FXC2_TOOLCHAIN/bin:$PATH"
CC=x86_64-w64-mingw32-clang
VULKAN_HEADERS="${FXC2_VULKAN_HEADERS:-/usr/include}"
SPIRV_HEADERS="${FXC2_SPIRV_HEADERS:-/usr/include}"
SRC="$WORK/vkd3d"
BUILD="$WORK/build"
XINC="$WORK/xinc"
REF=cfcb4833e27cb287e2d70b48e1bfe46713e04d51
mkdir -p "$WORK"
# Changing the patch set requires a fresh work directory; never reset a tree
# that may contain someone's work.
PATCH_ID="$(cat "$REPO"/patches/*.patch | sha256sum | cut -d' ' -f1)"
if [ ! -d "$SRC" ]; then
    git clone -q https://gitlab.winehq.org/wine/vkd3d.git "$SRC"
    git -C "$SRC" checkout -q "$REF"
    for p in "$REPO"/patches/*.patch; do
        git -C "$SRC" apply --whitespace=nowarn "$p"
    done
    printf '%s\n' "$PATCH_ID" > "$WORK/patch-id"
elif [ ! -f "$WORK/patch-id" ] || [ "$(cat "$WORK/patch-id")" != "$PATCH_ID" ]; then
    echo "Existing source has no matching patch marker; use a fresh FXC2_UNITY_WORK." >&2
    exit 1
fi
mkdir -p "$XINC"
for dir in vulkan vk_video; do
    [ -d "$XINC/$dir" ] || cp -r "$VULKAN_HEADERS/$dir" "$XINC/"
done
[ -d "$XINC/spirv" ] || cp -r "$SPIRV_HEADERS/spirv" "$XINC/"
[ -x "$SRC/configure" ] || (cd "$SRC" && ./autogen.sh)
RENAMES=""
for f in D3DCompile D3DCompile2 D3DReflect D3DGetBlobPart D3DStripShader D3DDisassemble D3DPreprocess; do
    RENAMES="$RENAMES -D$f=vkd3d_$f"
done
# Verify that the requested toolchain really uses UCRT.
MACROS="$("$CC" -dM -E -include stdio.h -x c /dev/null)"
if ! grep -q '^#define _UCRT ' <<< "$MACROS"; then
    echo "Toolchain is not configured for UCRT" >&2
    exit 1
fi
mkdir -p "$BUILD"
cd "$BUILD"
if [ ! -f Makefile ]; then
    "$SRC/configure" --host=x86_64-w64-mingw32 --disable-shared --enable-static \
        --disable-tests --disable-demos --disable-doxygen-doc \
        --without-xcb --without-ncurses --without-opengl CC="$CC" \
        CPPFLAGS="-I$XINC $RENAMES -DVKD3D_NO_TRACE_MESSAGES -DVKD3D_STATIC_LIB" \
        CFLAGS="-O2 -g0 -ffunction-sections -fdata-sections" \
        LDFLAGS="-static -Wl,--gc-sections" SONAME_LIBVULKAN=vulkan-1.dll
fi
if [ ! -f "$WORK/runtime-build" ]; then make clean; fi
printf 'fxc2-headers: $(BUILT_SOURCES)\n' > "$WORK/prepare.mk"
make -j"${FXC2_JOBS:-8}" -f Makefile -f "$WORK/prepare.mk" fxc2-headers
make -j"${FXC2_JOBS:-8}" libvkd3d-utils.la CPPFLAGS="-I$XINC $RENAMES -DVKD3D_NO_TRACE_MESSAGES -DVKD3D_STATIC_LIB -include $REPO/src/unity_runtime.h"
touch "$WORK/runtime-build"
FAST_FLOAT="${FXC2_FAST_FLOAT:-$REPO/out/fast_float}"
if [ ! -f "$FAST_FLOAT/include/fast_float/fast_float.h" ]; then
    git init -q "$FAST_FLOAT"
    git -C "$FAST_FLOAT" remote add origin https://github.com/fastfloat/fast_float.git
    git -C "$FAST_FLOAT" fetch -q --depth 1 origin 166d2b3df29cf8101bb715bde2125610881df791
    git -C "$FAST_FLOAT" checkout -q --detach FETCH_HEAD
fi
x86_64-w64-mingw32-clang++ -O2 -g0 -std=c++17 -fno-exceptions -fno-rtti \
    -I"$FAST_FLOAT/include" -include "$REPO/src/unity_runtime.h" \
    -c "$REPO/src/unity_float.cpp" -o "$WORK/unity_float.o"
cp "$REPO/src/d3dcompiler_47.def" "$WORK/unity.def"
printf '    D3DAssemble\n' >> "$WORK/unity.def"
"$CC" -O2 -g0 -static -shared -DFXC2_NO_TRACE -DFXC2_UNITY_LOADER \
    -fno-builtin -include "$REPO/src/unity_runtime.h" \
    -ffunction-sections -fdata-sections -Wl,--gc-sections,--exclude-all-symbols \
    -o "$WORK/D3DCompiler_47.dll" "$REPO/src/d3dcompiler_shim.c" \
    "$WORK/unity.def" "$REPO/src/unity_runtime.c" "$WORK/unity_float.o" \
    -L.libs -lvkd3d-utils -lvkd3d -lvkd3d-shader -lvkd3d-common
"$FXC2_TOOLCHAIN/bin/llvm-readobj" --coff-imports "$WORK/D3DCompiler_47.dll" > "$WORK/imports.txt"
echo "Experimental DLL: $WORK/D3DCompiler_47.dll"
echo "Audit imports and exports before testing in an isolated Unity tools copy."
