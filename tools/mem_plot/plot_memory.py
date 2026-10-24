#!/usr/bin/env python3
"""Plot the address ranges declared in a GNU ld script's MEMORY block."""

import argparse
import ast
from dataclasses import dataclass
import operator
from pathlib import Path
import re


DEFAULT_LINKER = Path(__file__).resolve().parents[2] / "STM32F769NIHX_FLASH.ld"
REGION_HEADER = re.compile(
    r"(?P<name>[A-Za-z_][\w.-]*)\s*"
    r"(?:\((?P<attributes>[rwxailRWXAIL!\s]*)\))?\s*:\s*"
    r"(?:ORIGIN|org|o)\s*=\s*"
)
OPERATORS = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.Div: operator.floordiv,
    ast.Mod: operator.mod,
    ast.LShift: operator.lshift,
    ast.RShift: operator.rshift,
    ast.BitOr: operator.or_,
    ast.BitAnd: operator.and_,
    ast.BitXor: operator.xor,
}


@dataclass(frozen=True)
class Region:
    name: str
    attributes: str
    origin: int
    length: int

    @property
    def end(self):
        """First address outside this region."""
        return self.origin + self.length


def format_size(size):
    for unit, divisor in (("GiB", 1024**3), ("MiB", 1024**2), ("KiB", 1024)):
        if size >= divisor:
            return f"{size / divisor:g} {unit}"
    return f"{size} B"


def evaluate_expression(expression):
    """Evaluate numeric linker expressions without executing Python code."""
    expression = re.sub(
        r"\b(0[xX][0-9a-fA-F]+|[0-9]+)([kKmM])\b",
        lambda m: f"({m[1]} * {1024 if m[2].upper() == 'K' else 1024**2})",
        expression,
    )

    def visit(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.UnaryOp):
            value = visit(node.operand)
            if isinstance(node.op, ast.UAdd):
                return value
            if isinstance(node.op, ast.USub):
                return -value
            if isinstance(node.op, ast.Invert):
                return ~value
        if isinstance(node, ast.BinOp) and type(node.op) in OPERATORS:
            left, right = visit(node.left), visit(node.right)
            if isinstance(node.op, (ast.LShift, ast.RShift)) and not 0 <= right < 64:
                raise ValueError("shift count must be between 0 and 63")
            return OPERATORS[type(node.op)](left, right)
        raise ValueError("only numeric constants and arithmetic are supported")

    try:
        return visit(ast.parse(expression.strip(), mode="eval").body)
    except (SyntaxError, ValueError, ZeroDivisionError) as error:
        raise ValueError(f"invalid expression {expression!r}: {error}") from error


def parse_regions(text):
    """Read MEMORY declarations; reject unsupported expressions explicitly."""
    text = re.sub(r"/\*.*?\*/|//[^\n]*|\#[^\n]*", " ", text, flags=re.DOTALL)
    memory = re.search(r"\bMEMORY\s*\{([^{}]*)\}", text)
    if memory is None:
        raise ValueError("no complete MEMORY { ... } block found")
    body = memory[1]
    headers = list(REGION_HEADER.finditer(body))
    if not headers or body[: headers[0].start()].strip():
        raise ValueError("MEMORY block is empty or contains an unsupported declaration")

    regions = []
    names = set()
    for index, header in enumerate(headers):
        stop = headers[index + 1].start() if index + 1 < len(headers) else len(body)
        fields = re.split(r",\s*(?:LENGTH|len|l)\s*=\s*", body[header.end() : stop])
        name = header["name"]
        if len(fields) != 2:
            raise ValueError(f"{name}: expected ORIGIN = expression, LENGTH = expression")
        if name in names:
            raise ValueError(f"duplicate memory region {name!r}")
        try:
            origin = evaluate_expression(fields[0])
            length = evaluate_expression(fields[1].strip().rstrip(";,"))
        except ValueError as error:
            raise ValueError(f"{name}: {error}") from error
        if origin < 0 or length <= 0 or origin + length > 2**64:
            raise ValueError(f"{name}: expected a nonnegative origin and positive length within 64-bit address space")
        names.add(name)
        regions.append(Region(name, "".join((header["attributes"] or "").split()), origin, length))
    return sorted(regions, key=lambda region: (region.origin, region.name))


def group_regions(regions):
    """Keep adjacent/overlapping ranges together, omitting large address gaps."""
    groups = []
    group_end = -1
    for region in sorted(regions, key=lambda item: item.origin):
        if not groups or region.origin > group_end:
            groups.append([])
        groups[-1].append(region)
        group_end = max(group_end, region.end)
    return groups


def find_overlaps(regions):
    return [
        (left, right)
        for index, left in enumerate(regions)
        for right in regions[index + 1 :]
        if left.origin < right.end and right.origin < left.end
    ]


def plot_regions(regions, title):
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter, MaxNLocator

    groups = group_regions(regions)
    overlaps = find_overlaps(regions)
    overlapping_names = {region.name for pair in overlaps for region in pair}
    colors = {region.name: plt.get_cmap("tab10")(index % 10) for index, region in enumerate(regions)}
    figure, axes = plt.subplots(
        len(groups), 1,
        figsize=(12, 2 + 1.1 * len(regions) + 0.7 * len(groups)),
        squeeze=False,
        gridspec_kw={"height_ratios": [len(group) + 0.9 for group in groups]},
        layout="constrained",
    )
    figure.suptitle(f"Memory regions — {title}", fontsize=16, fontweight="bold")

    for axis, group in zip(axes[:, 0], groups):
        base = group[0].origin
        end = max(region.end for region in group)
        for row, region in enumerate(group):
            overlap = region.name in overlapping_names
            axis.barh(
                row, region.length, left=region.origin - base, height=0.5,
                color=colors[region.name], edgecolor="#be123c" if overlap else "#334155",
                linewidth=2 if overlap else 0.8, hatch="///" if overlap else None,
            )
        axis.set_yticks(range(len(group)))
        axis.set_yticklabels([
            f"{region.name} ({region.attributes or 'no attributes'})\n{format_size(region.length)}"
            for region in group
        ])
        axis.set_ylim(len(group) - 0.4, -0.6)
        axis.set_xlim(0, end - base)
        # Plot relative offsets to avoid losing precision for large base addresses.
        axis.xaxis.set_major_formatter(FuncFormatter(lambda offset, _, base=base: f"0x{base + round(offset):08X}"))
        boundaries = sorted({address - base for region in group for address in (region.origin, region.end)})
        if len(boundaries) <= 6:
            axis.set_xticks(boundaries)
        else:
            axis.xaxis.set_major_locator(MaxNLocator(nbins=5, integer=True))
        axis.tick_params(axis="x", labelsize=9)
        axis.set_axisbelow(True)
        axis.grid(axis="x", linestyle=":", color="#94a3b8")
        axis.spines[["top", "right"]].set_visible(False)
        axis.set_title(
            f"0x{base:08X}–0x{end - 1:08X}  |  {format_size(end - base)} address span",
            loc="left", fontsize=11,
        )
        axis.set_xlabel("Address (upper boundary is exclusive)", fontsize=9)

    note = "Each panel has its own linear address scale; gaps between panels are omitted.\n"
    note += "Bars show configured capacity, not allocated sections or runtime usage."
    if overlaps:
        note += "  Hatched bars have overlapping address ranges."
    figure.supxlabel(note, fontsize=9, color="#475569")
    return figure


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("linker", type=Path, nargs="?", default=DEFAULT_LINKER,
                        help="linker script (default: this project's FLASH linker script)")
    parser.add_argument("-o", "--output", type=Path,
                        help="output PNG, SVG, or PDF (default: <script directory>/<linker stem>_memory.png)")
    parser.add_argument("--no-show", action="store_true", help="save without opening a plot window")
    args = parser.parse_args()

    try:
        regions = parse_regions(args.linker.read_text())
    except (OSError, UnicodeError, ValueError) as error:
        parser.error(str(error))

    print(f"{'Region':<18} {'Attributes':<12} {'Start':<18} {'Last byte':<18} Size")
    for region in regions:
        print(f"{region.name:<18} {region.attributes or '-':<12} 0x{region.origin:016X} 0x{region.end - 1:016X} {format_size(region.length)}")
    for left, right in find_overlaps(regions):
        print(f"Overlapping regions: {left.name} and {right.name}")

    try:
        import matplotlib
        if args.no_show:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        parser.error("matplotlib is required; install tools/mem_plot/requirements.txt")

    output = args.output or Path(__file__).resolve().parent / f"{args.linker.stem}_memory.png"
    figure = plot_regions(regions, args.linker.name)
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        figure.savefig(output, dpi=160)
    except (OSError, ValueError) as error:
        parser.error(f"could not save {output}: {error}")
    print(f"Saved plot to {output}")
    if not args.no_show:
        plt.show()
    plt.close(figure)


if __name__ == "__main__":
    main()
