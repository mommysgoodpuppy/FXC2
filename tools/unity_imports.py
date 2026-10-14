#!/usr/bin/env python3
"""Audit a DLL against the Linux UnityShaderCompiler's actual PE import table.

Requires pyelftools and llvm-readobj. This checks symbol names, not runtime
semantics, PE TLS handling, or the compiler's correctness.
"""
import argparse
import re
import subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile


def known_imports(executable, debug):
    with debug.open("rb") as stream:
        symbols = ELFFile(stream).get_section_by_name(".symtab")
        if symbols is None:
            raise ValueError("debug file has no symbol table")
        table = symbols.get_symbol_by_name("kWodkaKnownImports")[0]["st_value"]
        count_address = symbols.get_symbol_by_name("kWodkaKnownImportsCount")[0]["st_value"]
    with executable.open("rb") as stream:
        elf = ELFFile(stream)
        def bytes_at(address):
            for segment in elf.iter_segments():
                if segment["p_type"] == "PT_LOAD" and segment["p_vaddr"] <= address < segment["p_vaddr"] + segment["p_filesz"]:
                    offset = address - segment["p_vaddr"]
                    return segment.data()[offset:]
            raise ValueError(f"unmapped address {address:#x}")
        count = int.from_bytes(bytes_at(count_address)[:4], "little")
        relocations = {r["r_offset"]: r["r_addend"] for r in elf.get_section_by_name(".rela.dyn").iter_relocations()}
        # PEKnownImport is a name pointer followed by a function pointer.
        return {bytes_at(relocations[table + i * 16]).split(b"\0", 1)[0].decode("ascii") for i in range(count)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dll", type=Path)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--debug", type=Path)
    parser.add_argument("--readobj", default="llvm-readobj")
    args = parser.parse_args()
    debug = args.debug or args.compiler.with_name(args.compiler.name + "_s.debug")
    known = known_imports(args.compiler, debug)
    report = subprocess.check_output([args.readobj, "--coff-imports", str(args.dll)], text=True)
    imported = set(re.findall(r"Symbol: (\S+)", report))
    known_lower = {name.lower() for name in known}
    missing = sorted(name for name in imported if name.lower() not in known_lower)
    print(f"{len(imported)} imported names; {len(known)} Unity names; {len(missing)} outside table")
    print("Libraries: " + ", ".join(re.findall(r"Name: (\S+)", report)))
    for name in missing:
        print("  " + name)
    return bool(missing)


if __name__ == "__main__":
    raise SystemExit(main())
