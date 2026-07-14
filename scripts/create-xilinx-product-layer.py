#!/usr/bin/env python3
"""Create a Monutchee Xilinx product layer from a maintained template."""

from __future__ import annotations

import argparse
import re
import shutil
import sys
import tempfile
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
TEMPLATE_ROOTS = {
    "kr260": Path(__file__).resolve().parent / "templates" / "kr260-product",
}
PRODUCT_PATTERN = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
PREFIX_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
UNRESOLVED_TOKEN_PATTERN = re.compile(r"@@[A-Z0-9_]+@@")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create a Monutchee Xilinx product layer.",
    )
    parser.add_argument(
        "--product",
        required=True,
        help="lowercase product and Yocto machine identifier, for example msap1",
    )
    parser.add_argument(
        "--project-prefix",
        required=True,
        help="component repository/artifact prefix, for example MSAP1",
    )
    parser.add_argument(
        "--board",
        required=True,
        choices=sorted(TEMPLATE_ROOTS),
        help="Xilinx board template to render",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=REPOSITORY_ROOT,
        help="parent directory for meta-<product> (default: repository root)",
    )
    args = parser.parse_args()

    if not PRODUCT_PATTERN.fullmatch(args.product):
        parser.error(
            "--product must start with a lowercase letter and contain only "
            "lowercase letters, digits, and single hyphen-separated components"
        )
    if not PREFIX_PATTERN.fullmatch(args.project_prefix):
        parser.error(
            "--project-prefix must start with a letter and contain only "
            "letters, digits, and underscores"
        )
    return args


def render(value: str, replacements: dict[str, str]) -> str:
    for token, replacement in replacements.items():
        value = value.replace(token, replacement)
    unresolved = UNRESOLVED_TOKEN_PATTERN.search(value)
    if unresolved:
        raise ValueError(f"unresolved template token: {unresolved.group(0)}")
    return value


def rendered_relative_path(path: Path, replacements: dict[str, str]) -> Path:
    rendered = render(path.as_posix(), replacements)
    if rendered.endswith(".in"):
        rendered = rendered[:-3]
    return Path(rendered)


def create_layer(args: argparse.Namespace) -> Path:
    output_root = args.output_root.expanduser().resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    destination = output_root / f"meta-{args.product}"
    if destination.exists():
        raise FileExistsError(f"destination already exists: {destination}")

    template_root = TEMPLATE_ROOTS[args.board]
    if not template_root.is_dir():
        raise FileNotFoundError(f"template directory is missing: {template_root}")

    replacements = {
        "@@PRODUCT@@": args.product,
        "@@PROJECT_PREFIX@@": args.project_prefix,
        "@@PROJECT_LABEL@@": args.project_prefix.upper(),
        "@@LAYER_VARIABLE@@": args.product.replace("-", "_").upper(),
        "__PRODUCT__": args.product,
    }

    staging = Path(
        tempfile.mkdtemp(prefix=f".meta-{args.product}.", dir=output_root)
    )
    try:
        for source in sorted(template_root.rglob("*")):
            if not source.is_file():
                continue
            relative = rendered_relative_path(
                source.relative_to(template_root), replacements
            )
            target = staging / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(render(source.read_text(), replacements))
            target.chmod(0o644)
            if target.name == "mncos-flash-emmc":
                target.chmod(0o755)

        shutil.copy2(REPOSITORY_ROOT / "LICENSE", staging / "LICENSE")
        (staging / "LICENSE").chmod(0o644)
        staging.rename(destination)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise

    return destination


def main() -> int:
    args = parse_arguments()
    try:
        destination = create_layer(args)
    except (FileExistsError, FileNotFoundError, OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"Created {destination}")
    print(
        "Next: review the generated README.md and add the product build profile "
        "to monutchee-manifest."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
