"""Build and consume every project template with the supplied ngin executable."""

from concurrent.futures import ThreadPoolExecutor
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def check_template(cli, root, kind, layout):
    def ngin(directory, *args):
        result = subprocess.run([cli, *args], cwd=directory, text=True, capture_output=True)
        if result.returncode:
            raise RuntimeError(f"{kind}/{layout}: ngin {' '.join(args)}\n{result.stdout}{result.stderr}")
        return result.stdout

    args = ["new", kind, "Demo", "--layout", layout]
    if layout == "custom":
        args += ["--source-root", "Engine/Private", "--header-root", "Public API"]
    preview = json.loads(ngin(root, *args, "--format", "json"))
    product = root / "Demo"
    assert not product.exists()
    ngin(root, *args)
    files = {p.relative_to(product).as_posix(): p.read_bytes().decode("utf-8")
             for p in product.rglob("*") if p.is_file()}
    assert files == preview["files"], f"CLI and editor scaffolds differ for {kind}/{layout}"
    ngin(product, "build")
    if kind == "executable":
        return

    ngin(root, "new", "executable", "Consumer")
    consumer = root / "Consumer"
    if kind == "plugin":
        # A plugin is loaded at runtime. Independently compile its public API without linking it.
        header = os.path.relpath(next(product.rglob("Demo.hpp")), consumer / "src")
        code = (f'#include "{Path(header).as_posix()}"\n#include <type_traits>\n'
                'static_assert(std::is_same_v<decltype(Demo_version()), int>);\nint main() {}\n')
    else:
        (consumer / "Consumer.nginproj").write_text(
            '<Executable Name="Consumer"><Uses><Project Path="../Demo/Demo.nginproj" /></Uses></Executable>',
            encoding="utf-8")
        code = "#include <Demo.hpp>\nint main() { return Demo_version() != 1; }\n"
    (consumer / "src/main.cpp").write_text(code, encoding="utf-8")
    ngin(consumer, "build")


if __name__ == "__main__":
    executable = Path(sys.argv[1]).resolve()
    if os.name == "nt" and not executable.suffix:
        executable = executable.with_suffix(".exe")
    def run_case(case):
        kind, layout = case
        with tempfile.TemporaryDirectory() as directory:
            check_template(executable, Path(directory), kind, layout)
        return f"Passed {kind}/{layout}"

    cases = itertools.product(("executable", "static", "shared", "interface", "plugin"),
                              ("split", "colocated", "public-private", "custom"))
    with ThreadPoolExecutor(max_workers=2) as workers:
        for message in workers.map(run_case, cases):
            print(message, flush=True)
