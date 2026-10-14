# FXC2 inside Linux Unity

Tested on 14 October 2026 with Unity 2022.3.22f1 for Linux, FXC2 `2fd671f`,
and vkd3d `cfcb4833` plus this repository's patches. The automated compilation
checks use a disposable editor/project under `out/unity-linux/`.

## Current result

**Native Linux Unity successfully builds Windows/D3D11 shader bundles with
this experimental FXC2 DLL.** Wine is not involved in Unity compilation.
The compiler remains a PE DLL because Unity's own shader compiler loads that
format directly. The UCRT toolchain is used to build it on Linux.

Verified:

- FXC2's unique `mulhi()` intrinsic compiles inside Unity.
- ShaderEmu's actual `MachineTick.shader` compiles, including its local-array
  CPUTick, vertex, and geometry programs. Its pixel DXBC is about 575 KB.
- The vrmodem downlink decoder compiles as D3D11 DXBC.
- Regression bundle compilation also covers Blink, MemHeat, all eight GpuDraw
  passes, VRChat's Supersampled UI, and Standard (86 internal programs). The
  final check reports no compiler crashes or reached missing imports.
- Sprites/Default also compiles in a separate uncached built-in check (eight
  internal programs), with no crashes or reached missing imports.
- Runtime helper tests pass, including allocation overflow, sorting/search,
  integer parsing, decimal float boundaries, 20,000 comparisons against the
  original UCRT float parser under Wine, and independent unroll-retry settings
  on two threads.
- Captured Unity HLSL was also compiled with the repository's existing FXC2 DLL
  under Wine. Five of seven executable instruction chunks match exactly,
  including both downlink stages. The other two (CPUTick pixel and geometry)
  have identical disassembled instructions after temporary-register numbers
  are normalized. Unity strips reflection/statistics, so whole-container
  hashes are not expected to match the standalone compiler's output.

Subsequent integration testing reports a working ShaderEmu world, including a
live telnet connection into its Linux guest over the modem. That validates the
practical emulator/modem workload beyond bundle compilation.

**Not yet comprehensively verified:** emulator state equivalence, OpenGL
compilation, all ShaderEmu shader passes, and arbitrary Unity projects.
Instruction comparison is useful evidence, not a substitute for execution
checks. The local sort implementation permits register allocation to differ
from the old CRT's qsort.

The DLL is `out/unity-linux/D3DCompiler_47.dll`. Keep it in an isolated editor
while validating a new workload. It is not yet a general D3DCompiler replacement.

## What changed

The underlying compiler needed no Linux rewrite. The platform layer consists
of roughly 320 lines of C/C++ (excluding the existing compiler/API shim):

- UCRT instead of msvcrt; shim file tracing compiled out with FXC2_NO_TRACE.
- VKD3D_STATIC_LIB header switch permits Clang static linking without exporting
  unrelated vkd3d/D3D12 APIs or retaining their dependencies.
- Strings, overflow-checked zeroed allocation, sorting/search, and errno storage
  implemented locally for calls missing from Unity's PE loader.
- String formatting uses MinGW's linked C99 formatter, bypassing Unity's broken
  UCRT formatting bridge. C-locale metadata and byte/wide conversion helpers
  are supplied locally; arbitrary host-locale formatting is not supported.
- exp2f constant folding uses Unity's supported double-precision pow import.
- C-locale decimal float parsing provided by fast_float, pinned at
  `166d2b3df29cf8101bb715bde2125610881df791`. This is exactly the format used by
  vkd3d's HLSL lexer; it is not a general hexadecimal strtof implementation.
- The compiler's unroll-retry environment override is local to each thread via
  Unity's supported TLS imports. Other environment reads still use the host.
- A Unity-only D3DAssemble export explicitly returns E_NOTIMPL. vkd3d has no
  textual DXBC assembly input, but Unity checks for this export even in HLSL
  builds. No real assembly support is claimed.

The original GCC/Windows build's defaults are unchanged.

## Reproduce the build

On an x86_64 Linux host, install the native build tools first: a C compiler,
make, git, curl, tar (with xz), sha256sum, autoconf, automake, libtoolize, flex,
bison, pkg-config, Perl, and Wine's widl. For Debian/Ubuntu these are supplied
by `build-essential git curl xz-utils autoconf automake libtool flex bison
pkg-config perl wine64-tools libwine-dev`. Package names vary by distribution;
the bootstrap script checks tools and never installs system packages.

From a fresh checkout:

```sh
bash scripts/bootstrap-unity-linux.sh
source out/unity-linux-env.sh
bash scripts/build-unity-linux.sh

# Optional helper regression tests; requires Wine, using a separate prefix.
bash scripts/test-unity-runtime.sh

# Native Unity compilation; requires Python 3 and a licensed Linux editor.
python3 tools/check_unity_linux.py \
  --editor /path/to/2022.3.22f1/Editor \
  --debug-loader \
  --builtin Standard --builtin Sprites/Default

# Optional project-specific coverage, with source checkouts you supply.
python3 tools/check_unity_linux.py \
  --editor /path/to/2022.3.22f1/Editor \
  --shaderemu /path/to/ShaderEmu \
  --downlink /path/to/vrmodem/unity \
  --shader '/path/to/VRChat-Mobile-SupersampledUnlit.shader'
```

The bootstrap pins LLVM-MinGW UCRT 20261006 and JSON 4.10 archives with SHA-256
checks, and Vulkan-Headers, SPIRV-Headers, and fast_float to exact commits.
It writes dependency checkouts and an environment file under `out/`.
The compiler core is pinned to
`cfcb4833e27cb287e2d70b48e1bfe46713e04d51`; the build applies every patch in
`patches/`. A network connection is needed for the initial downloads, not
for subsequent shader compilations. Custom dependencies can instead be
provided through FXC2_TOOLCHAIN, FXC2_VULKAN_HEADERS, FXC2_SPIRV_HEADERS,
FXC2_FAST_FLOAT, and PERL5LIB. FXC2_UNITY_WORK selects a separate build tree.

The check creates a disposable editor overlay and project, copies actual
sources and includes, and builds a Windows shader bundle. It does not export
a world, launch VRChat, or alter the installed editor. It requires the editor
closed on its *disposable* project and a working Unity license. No display or
Wine is needed for this batch compilation test. The Windows build-support
module is required. `--debug-loader` additionally requires GDB and the
editor's matching `UnityShaderCompiler_s.debug` file. Omit it for a compilation
check without import tracing.

New shader names/constants and the disposable project's ShaderCache reset
avoid reusing old failures. ShaderEmu sources are staged the same way as its
SyncShaders method: .h includes become .cginc. Build output is inspected for
shader errors in addition to the manifest and process exit status, because
Unity can otherwise build a bundle containing invalid shader programs.
The check has a timeout that terminates only its disposable editor and worker
process group. `--debug-loader` generates its wrapper and commands inside the
disposable editor, logs to `out/unity-linux/loader-check.log`, and rejects
reached missing imports and SIGSEGVs in that run. With an existing external
debugger wrapper, `--loader-log /path/to/log` provides the same log checks.
Built-in tests
clear the disposable project's always-included list to force bundle compilation.

Use a fresh FXC2_UNITY_WORK after changing patches. The script refuses to
reset an existing source tree with mismatched patches.

## Remaining import surface

`tools/unity_imports.py` compares imported names with the actual
kWodkaKnownImports table extracted from UnityShaderCompiler and its matching
_s.debug file. Requires pyelftools and llvm-readobj:

```sh
python3 tools/unity_imports.py out/unity-linux/D3DCompiler_47.dll \
  --readobj "$FXC2_TOOLCHAIN/bin/llvm-readobj" \
  --compiler /path/to/Editor/Data/Tools/UnityShaderCompiler
```

Names are compared case-insensitively, as Unity does. The audit exits nonzero
for any name outside the table; that does not mean the DLL cannot map. Unity
creates failure thunks for unknown imports, which are a problem when called.
The current DLL still has 32 such imports, primarily file APIs, extra
math functions, and failure-handling paths. They were not reached by the
successful shader builds above. Do not replace them with no-ops to claim full
support. Shader include/file APIs outside Unity's supplied source/include
workflow are not supported by the adapter yet.

## Earlier failure and debugger evidence

A full world export with the earlier experimental DLL produced 145 compiler
crashes, after which the stock compiler was restored for that project.
The repeated stack passed through Unity's `_vsnprintf_impl`
and libc strlen (reported as `_dl_mcount_wrapper` in the editor's crash report).

An isolated Supersampled UI build reproduced this. The problematic format was
`.%s%c%c%c%c%s`, used when disassembling swizzles. Unity 2022.3.22f1's shim
advances the Windows va_list for `%c` only inside the branch that writes the
character (native offsets 0x2bbdcb–0x2bbddb). If the destination is full, it skips
the argument; the trailing `%s` dereferences a character value as a pointer.
The reproduction stopped in libc strlen with a pointer such as 0x7fff00000077.
This is a formatting compatibility failure, not evidence that FXC2 cannot
compile those ordinary shaders. Local string formatting bypasses the shim.

Regression tests cover every truncation boundary and NULL/zero-size counting,
embedded NUL characters, width/precision, integers, and floating-point output.
Broadening the bundle also reached the missing exp2f import during constant
folding; this is now implemented via pow. Missing-import thunks can return
without crashing, so a bundle's PASS alone is insufficient.

Expanded D3D11 checks passed with no reached missing imports or crashes.
Sprites/Default was also forced into a separate bundle check. These automated
checks do not establish OpenGL support or arbitrary world-export coverage.

The first UCRT/no-tracing build mapped, but Unity's IsValid rejected it because
D3DAssemble was missing. A probe-only assembly export exposed actual calls to
_putenv, strlen, calloc, then strcspn/errno and C-locale routines. These were
fixed in the platform layer rather than in the compiler core.

Local artifacts:

- check-*.log: each repeatable native Unity test.
- loader-check.log and loader-check.gdb: reproducible reached-import tracing,
  using the matching editor's debug symbols.
- runtime-test.exe: helper test executable, run under an isolated Wine prefix.
- imports.txt: the final import table.

Older smoke.log/smoke-gdb.log files predate error-log handling and printed PASS
for invalid bundles. They are superseded by the repeatable check-*.log tests.
A ShaderLab #error guarded by __FXC2__ also proved unsuitable as a compiler
identity test: Unity preprocesses it before FXC2 sees it. The mulhi probe
exercises a compiler feature directly instead. It is a compilation-only test;
ShaderEmu avoids that intrinsic in shipped shaders because of a separate Linux
DXBC-to-SPIR-V translation bug.

## Which Linux route to pursue

A native ELF FXC2 CLI is a sensible independent tool, and the compiler core
already supports that platform. The small frontend still needs Windows-header
and environment-call adaptation. Running the existing CLI under Wine is also
useful for comparisons. Neither executable replaces the DLL entry points in
Unity's shader pipeline automatically: integration must account for Unity's
preprocessing, shader stages/variants, reflection, diagnostics, and bundles.

For the Unity/VRChat workflow, the PE-DLL compatibility layer now has successful
compilation checks and a working emulator/modem integration report. The earlier
world-export failure shows that runtime paths matter beyond the machine shaders.
Test new renderers and projects in an isolated setup, and preserve a stock
compiler fallback while extending coverage.
