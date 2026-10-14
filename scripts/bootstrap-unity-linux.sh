#!/usr/bin/env bash
# Download pinned build dependencies locally; never install system packages.
# Run from an x86_64 Linux host, then source out/unity-linux-env.sh.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$REPO/out/unity-linux-deps"
if [ "$(uname -m)" != x86_64 ]; then
    echo "The pinned LLVM-MinGW archive requires an x86_64 Linux host." >&2
    exit 1
fi
for tool in curl git tar sha256sum make cc autoconf automake libtoolize flex bison pkg-config widl perl; do
    if ! command -v "$tool" >/dev/null; then
        echo "Missing host tool: $tool (see docs/unity-linux-loader.md)" >&2
        exit 1
    fi
done
mkdir -p "$DEPS"
download() {
    local url="$1" file="$2" digest="$3"
    if [ ! -f "$file" ]; then
        curl -fL --retry 3 "$url" -o "$file.part"
        printf '%s  %s\n' "$digest" "$file.part" | sha256sum --check --status
        mv "$file.part" "$file"
    fi
    printf '%s  %s\n' "$digest" "$file" | sha256sum --check --status
}
pinned_tree() {
    local url="$1" path="$2" revision="$3"
    if [ ! -d "$path" ]; then
        git init -q "$path"
        git -C "$path" remote add origin "$url"
        git -C "$path" fetch -q --depth 1 origin "$revision"
        git -C "$path" checkout -q --detach FETCH_HEAD
    fi
    if [ "$(git -C "$path" rev-parse HEAD)" != "$revision" ] ||
            ! git -C "$path" diff --quiet HEAD; then
        echo "Dependency tree differs from its pin: $path; refusing to reset it." >&2
        exit 1
    fi
}
TOOLCHAIN=llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64
download "https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/$TOOLCHAIN.tar.xz" \
    "$DEPS/$TOOLCHAIN.tar.xz" 5f9c6ed95b2d4bdb2869a488c5fd5857fbdabcf288a0aa3eb1da43f6a08d8ab4
if [ ! -d "$DEPS/$TOOLCHAIN" ]; then tar -xJf "$DEPS/$TOOLCHAIN.tar.xz" -C "$DEPS"; fi
download https://cpan.metacpan.org/authors/id/I/IS/ISHIGAKI/JSON-4.10.tar.gz \
    "$DEPS/JSON-4.10.tar.gz" df8b5143d9a7de99c47b55f1a170bd1f69f711935c186a6dc0ab56dd05758e35
if [ ! -d "$DEPS/JSON-4.10" ]; then tar -xzf "$DEPS/JSON-4.10.tar.gz" -C "$DEPS"; fi
pinned_tree https://github.com/KhronosGroup/Vulkan-Headers.git "$DEPS/Vulkan-Headers" \
    c46850864f4661461b0f6cb9922c058ffea4915e
pinned_tree https://github.com/KhronosGroup/SPIRV-Headers.git "$DEPS/SPIRV-Headers" \
    e867c06631767a2d96424cbec530f9ee5e78180f
pinned_tree https://github.com/fastfloat/fast_float.git "$DEPS/fast_float" \
    166d2b3df29cf8101bb715bde2125610881df791
{
    printf 'export FXC2_TOOLCHAIN=%q\n' "$DEPS/$TOOLCHAIN"
    printf 'export FXC2_VULKAN_HEADERS=%q\n' "$DEPS/Vulkan-Headers/include"
    printf 'export FXC2_SPIRV_HEADERS=%q\n' "$DEPS/SPIRV-Headers/include"
    printf 'export FXC2_FAST_FLOAT=%q\n' "$DEPS/fast_float"
    printf 'export PERL5LIB=%q${PERL5LIB:+:$PERL5LIB}\n' "$DEPS/JSON-4.10/lib"
} > "$REPO/out/unity-linux-env.sh"
echo "Dependencies ready. Run: source out/unity-linux-env.sh"
