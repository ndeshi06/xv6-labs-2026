#!/usr/bin/env python3

import json
import re
import subprocess
import sys
from pathlib import Path


def run_command(command):
    result = subprocess.run(
        command,
        text=True,
        capture_output=True
    )

    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip())

    return result.stdout


def main():
    if len(sys.argv) != 5:
        print(
            "Usage: gen_backtrace_map.py "
            "OBJDUMP ADDR2LINE ELF OUTPUT"
        )
        return 1

    objdump, addr2line, elf, output = sys.argv[1:]

    # Get all instruction addresses from kernel disassembly.
    disassembly = run_command([objdump, "-d", elf])

    pattern = re.compile(
        r"^\s*([0-9a-fA-F]+):\s+"
        r"(?:[0-9a-fA-F]{2,8}\s+)+"
    )

    addresses = sorted({
        int(match.group(1), 16)
        for line in disassembly.splitlines()
        if (match := pattern.match(line))
    })

    if not addresses:
        raise RuntimeError("No instruction addresses found")

    records = []

    # Resolve addresses in batches to avoid a long command line.
    for start in range(0, len(addresses), 500):
        batch = addresses[start:start + 500]

        command = [
            addr2line,
            "-a", "-f", "-C",
            "-e", elf,
            *[hex(addr) for addr in batch]
        ]

        lines = run_command(command).splitlines()

        if len(lines) != len(batch) * 3:
            raise RuntimeError(
                "Unexpected addr2line output format"
            )

        for i in range(0, len(lines), 3):
            address = int(lines[i], 16)
            function = lines[i + 1]
            location = lines[i + 2]

            match = re.match(
                r"^(.*):(\d+)"
                r"(?:\s+\(discriminator \d+\))?$",
                location
            )

            if function == "??" or match is None:
                continue

            source = match.group(1)
            line_number = int(match.group(2))

            if line_number <= 0:
                continue

            if source.startswith("./"):
                source = source[2:]

            records.append(
                (address, function, source, line_number)
            )

    records.sort(key=lambda item: item[0])

    functions = sorted({item[1] for item in records})
    files = sorted({item[2] for item in records})

    function_ids = {
        name: index for index, name in enumerate(functions)
    }

    file_ids = {
        name: index for index, name in enumerate(files)
    }

    output_lines = []

    output_lines.append("/* Generated automatically. Do not edit. */")
    output_lines.append("")
    output_lines.append("static const char * const bt_functions[] = {")

    for name in functions:
        output_lines.append("  " + json.dumps(name) + ",")

    output_lines.append("};")
    output_lines.append("")
    output_lines.append("static const char * const bt_files[] = {")

    for name in files:
        output_lines.append("  " + json.dumps(name) + ",")

    output_lines.append("};")
    output_lines.append("")
    output_lines.append("static const struct bt_entry bt_entries[] = {")

    for address, function, source, line_number in records:
        output_lines.append(
            "  { "
            f"0x{address:016x}ULL, "
            f"{function_ids[function]}, "
            f"{file_ids[source]}, "
            f"{line_number}"
            " },"
        )

    output_lines.append("};")
    output_lines.append("")
    output_lines.append(
        "#define BT_ENTRY_COUNT "
        "(sizeof(bt_entries) / sizeof(bt_entries[0]))"
    )

    output_path = Path(output)
    content = "\n".join(output_lines) + "\n"

    if not output_path.exists() or output_path.read_text(
        encoding="utf-8"
    ) != content:
        output_path.write_text(content, encoding="utf-8")

    print(
        f"Generated {len(records)} mappings "
        f"from {len(addresses)} instructions."
    )

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
