#!/usr/bin/env python3
"""Require a Doxygen summary on each installed public NGIN.UI type."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


TYPE_DECLARATION = re.compile(
    r"^(?P<indent> *)(?:(?:enum\s+class)|class|struct)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\b"
)
TYPE_ALIAS = re.compile(
    r"^(?P<indent> *)(?:template\s*<[^;]+>\s*)?using\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*="
)
ACCESS_LABEL = re.compile(r"^  (?P<access>public|protected|private):\s*$")


def _previous_documentation(lines: list[str], index: int) -> bool:
    candidate = index - 1
    while candidate >= 0:
        stripped = lines[candidate].strip()
        if not stripped or stripped.startswith("template ") or stripped.startswith(
            "requires "
        ):
            candidate -= 1
            continue
        return stripped.startswith("///") or stripped.endswith("*/")
    return False


def _declarations(path: Path) -> list[tuple[int, str, bool]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    declarations: list[tuple[int, str, bool]] = []
    brace_depth = 0
    outer_body_depth: int | None = None
    outer_access = "private"

    for index, line in enumerate(lines):
        if outer_body_depth is not None and brace_depth < outer_body_depth:
            outer_body_depth = None

        access = ACCESS_LABEL.match(line)
        if (
            access is not None
            and outer_body_depth is not None
            and brace_depth == outer_body_depth
        ):
            outer_access = access.group("access")

        match = TYPE_DECLARATION.match(line) or TYPE_ALIAS.match(line)
        if match is not None:
            indent = match.group("indent")
            namespace_type = indent == ""
            public_nested_type = (
                indent == "  "
                and outer_body_depth is not None
                and brace_depth == outer_body_depth
                and outer_access == "public"
            )
            if namespace_type or public_nested_type:
                declarations.append(
                    (
                        index + 1,
                        match.group("name"),
                        _previous_documentation(lines, index),
                    )
                )

            if namespace_type and "{" in line and not line.rstrip().endswith(";"):
                kind = line.split(maxsplit=1)[0]
                outer_body_depth = brace_depth + 1
                outer_access = "public" if kind == "struct" else "private"

        brace_depth += line.count("{") - line.count("}")

    return declarations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "include_root",
        type=Path,
        nargs="+",
        help="Directory containing the installed NGIN/UI public headers",
    )
    arguments = parser.parse_args()

    failures: list[str] = []
    declaration_count = 0
    for include_root in arguments.include_root:
        for path in sorted(include_root.rglob("*.hpp")):
            for line, name, documented in _declarations(path):
                declaration_count += 1
                if not documented:
                    failures.append(
                        f"{path}:{line}: public type '{name}' has no Doxygen comment"
                    )

    if failures:
        print("\n".join(failures), file=sys.stderr)
        print(
            f"NGIN.UI API documentation check failed: "
            f"{len(failures)} of {declaration_count} public types are undocumented.",
            file=sys.stderr,
        )
        return 1

    print(
        f"NGIN.UI API documentation check passed: "
        f"{declaration_count} public types documented."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
