#!/usr/bin/env python3
"""Build test shaders using a disposable Linux Unity editor/project overlay.

Never replaces the installed compiler or opens the real ShaderEmu project.
--shaderemu copies its machine, GPU, Blink, and MemHeat sources/includes.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import shlex
import signal
import subprocess
import time


def overlay(editor, destination):
    (destination / "Data/Tools").mkdir(parents=True, exist_ok=True)
    for source in editor.iterdir():
        if source.name == "Data":
            continue
        target = destination / source.name
        if target.exists():
            continue
        if source.name == "Unity":
            shutil.copy2(source, target)
        else:
            target.symlink_to(source, target_is_directory=source.is_dir())
    for source in (editor / "Data").iterdir():
        if source.name != "Tools" and not (destination / "Data" / source.name).exists():
            (destination / "Data" / source.name).symlink_to(source, target_is_directory=source.is_dir())
    for source in (editor / "Data/Tools").iterdir():
        if source.name in ("D3DCompiler_47.dll", "UnityShaderCompiler"):
            continue
        target = destination / "Data/Tools" / source.name
        if not target.exists():
            target.symlink_to(source, target_is_directory=source.is_dir())
    compiler = destination / "Data/Tools/UnityShaderCompiler"
    # Preserve the local debugger wrapper when requested by the caller.
    if not compiler.exists():
        shutil.copy2(editor / "Data/Tools/UnityShaderCompiler", compiler)


def main():
    repo = Path(__file__).resolve().parent.parent
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--editor", type=Path, default=Path.home() / "Unity/Hub/Editor/2022.3.22f1/Editor")
    p.add_argument("--work", type=Path, default=repo / "out/unity-linux")
    p.add_argument("--dll", type=Path)
    p.add_argument("--shaderemu", type=Path)
    p.add_argument("--downlink", type=Path)
    p.add_argument("--shader", type=Path, action="append", default=[],
        help="Additional standalone shader to compile (repeatable)")
    p.add_argument("--timeout", type=int, default=300,
        help="Maximum seconds for the disposable editor run")
    p.add_argument("--loader-log", type=Path,
        help="Debugger log to check for reached missing imports and crashes")
    p.add_argument("--debug-loader", action="store_true",
        help="Wrap the disposable compiler with GDB and reject missing imports/crashes")
    p.add_argument("--builtin", action="append", default=[],
        help="Built-in shader to include via a material (repeatable)")
    args = p.parse_args()
    work = args.work.resolve()
    editor = work / "editor"
    project = work / "project"
    if (project / "Temp/UnityLockfile").exists():
        raise RuntimeError("Disposable project is already open")
    overlay(args.editor.resolve(), editor)
    if args.debug_loader:
        debugger = shutil.which("gdb")
        if not debugger:
            raise RuntimeError("--debug-loader requires GDB and the editor's matching _s.debug file")
        if args.loader_log:
            raise RuntimeError("Use --debug-loader or an external --loader-log, not both")
        args.loader_log = work / "loader-check.log"
        real = editor / "Data/Tools/UnityShaderCompiler.real"
        shutil.copy2(args.editor.resolve() / "Data/Tools/UnityShaderCompiler", real)
        commands = work / "loader-check.gdb"
        commands.write_text("\n".join([
            "set pagination off", "set confirm off", "set debuginfod enabled off",
            "set logging file " + str(args.loader_log),
            "set logging overwrite off", "set logging redirect on", "set logging enabled on",
            "break _Z20FailedImportProcImplPKc", "commands", "silent",
            'printf "FXC2_FAILED_IMPORT: %s\\n", (char*)$rcx', "continue", "end",
            "handle SIGSEGV stop nopass", "run", "python",
            "if gdb.selected_inferior().pid:",
            "    gdb.execute('bt')", "    gdb.execute('kill')", "end", "",
        ]))
        wrapper = editor / "Data/Tools/UnityShaderCompiler"
        wrapper.write_text("#!/bin/sh\nexec " + shlex.join([
            debugger, "-q", "-nx", "-batch", "-x", str(commands), "--args", str(real)]) + ' "$@"\n')
        wrapper.chmod(0o755)
    dll = args.dll or work / "D3DCompiler_47.dll"
    shutil.copy2(dll, editor / "Data/Tools/D3DCompiler_47.dll")
    (project / "Assets/Editor").mkdir(parents=True, exist_ok=True)
    (project / "ProjectSettings").mkdir(exist_ok=True)
    (project / "ProjectSettings/ProjectVersion.txt").write_text("m_EditorVersion: 2022.3.22f1\n")
    shutil.copy2(repo / "tools/unity_loader_smoke.cs", project / "Assets/Editor/CompilerSmoke.cs")
    # A new identifier/constant forces an uncached source even after failed
    # compilation of an older DLL. Only the disposable project's cache is reset.
    stamp = str(time.time_ns())
    salt = int(hashlib.sha256((stamp + str(dll)).encode()).hexdigest()[:8], 16)
    source = (repo / "tests/unity_loader_smoke.shader").read_text()
    source = source.replace("FXC2/Smoke", "FXC2/Smoke" + stamp).replace("0xfffffffdu", hex(salt) + "u")
    (project / "Assets/Smoke.shader").write_text(source)
    assets = ["Assets/Smoke.shader"]
    for index, shader in enumerate(args.shader):
        name = "Regression" + str(index) + ".shader"
        source = shader.read_text().replace('Shader "', 'Shader "LoaderProbe' + stamp + '/')
        (project / "Assets" / name).write_text(source)
        assets.append("Assets/" + name)
    if args.shaderemu:
        root = args.shaderemu.resolve()
        shaders = root / "unity/ShaderEmu/Shaders"
        target = project / "Assets/ShaderEmu"
        target.mkdir(exist_ok=True)
        for name in ("MachineTick.shader", "MachineBlit.cginc", "GpuDraw.shader",
                "GpuDrawPass.cginc", "Blink.shader", "MemHeat.shader"):
            shutil.copy2(shaders / name, target / name)
        cpu = (target / "MachineTick.shader").read_text().replace(
            '#pragma target 5.0', '#pragma target 5.0\n            #pragma only_renderers d3d11')
        cpu = cpu.replace('Shader "', 'Shader "LoaderProbe' + stamp + '/')
        (target / "MachineTick.shader").write_text(cpu)
        upstream = root / "experiments/rvc_opt"
        shutil.copy2(upstream / "helpers.cginc", target / "helpers.cginc")
        (target / "src").mkdir(exist_ok=True)
        for header in (upstream / "src").glob("*.h"):
            text = re.sub(r'(#include\s+"[^"]+)\.h"', r'\1.cginc"', header.read_text())
            (target / "src" / (header.stem + ".cginc")).write_text(text)
        assets.append("Assets/ShaderEmu/MachineTick.shader")
        for name in ("GpuDraw.shader", "Blink.shader", "MemHeat.shader"):
            source = (target / name).read_text().replace('Shader "', 'Shader "LoaderProbe' + stamp + '/')
            (target / name).write_text(source)
            assets.append("Assets/ShaderEmu/" + name)
    if args.downlink:
        target = project / "Assets/Downlink"
        target.mkdir(exist_ok=True)
        for name in ("Downlink.shader", "Downlink.cginc"):
            shutil.copy2(args.downlink / name, target / name)
        source = (target / "Downlink.shader").read_text().replace(
            '#pragma target 5.0', '#pragma target 5.0\n            #pragma only_renderers d3d11')
        (target / "Downlink.shader").write_text(source.replace('Shader "', 'Shader "LoaderProbe' + stamp + '/'))
        assets.append("Assets/Downlink/Downlink.shader")
    for name in ("ShaderCache", "ShaderCache.db"):
        path = project / "Library" / name
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()
    env = {**os.environ, "FXC2_SMOKE_ASSETS": ";".join(assets),
        "FXC2_SMOKE_BUILTINS": ";".join(args.builtin)}
    log = work / ("check-" + stamp + ".log")
    offset = args.loader_log.stat().st_size if args.loader_log and args.loader_log.exists() else 0
    process = subprocess.Popen([str(editor / "Unity"), "-batchmode", "-quit", "-nographics",
        "-projectPath", str(project), "-executeMethod", "CompilerSmoke.Run", "-logFile", str(log)], env=env,
        start_new_session=True)
    try:
        status = process.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        # Kill only this disposable editor's process group, including workers.
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
        print("Disposable editor timed out. Log:", log)
        return 1
    text = log.read_text()
    errors = [line for line in text.splitlines() if "Shader error in" in line or "[CompilerSmoke]" in line]
    print("\n".join(errors))
    print("Log:", log)
    loader_errors = []
    if args.loader_log:
        with args.loader_log.open("rb") as stream:
            stream.seek(offset)
            loader_errors = [line for line in stream.read().decode(errors="replace").splitlines()
                if "FXC2_FAILED_IMPORT:" in line or "received signal SIGSEGV" in line]
        print("\n".join(sorted(set(loader_errors))))
    return int(status != 0 or "Shader error in" in text or "[CompilerSmoke] PASS" not in text or bool(loader_errors))


if __name__ == "__main__":
    raise SystemExit(main())
