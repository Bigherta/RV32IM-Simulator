#!/usr/bin/env python3
"""Synchronize the unmodified course corpus and validate full AXI exit results."""
import argparse
import fnmatch
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FILES = ("program.data", "program.S", "expected.txt", "metrics.json")
BRANCH_TYPES = ("all", "cond", "jal", "jalr")


def parse_branches(log, name):
    """Read the historical branch:/branch-type: protocol, not rounded rates."""
    def line_for(prefix, required=True):
        lines = [line for line in log.splitlines() if line.startswith(prefix)]
        if len(lines) > 1 or (required and not lines):
            raise ValueError(f"{name}: expected one {prefix} statistic")
        return lines[0] if lines else None

    summary = line_for("branch:")
    match = re.fullmatch(r"branch:\s+(\d+)/(\d+) correct \(\d+(?:\.\d+)?%\)", summary)
    if not match:
        raise ValueError(f"{name}: malformed branch statistic: {summary}")
    stats = dict.fromkeys(BRANCH_TYPES)
    stats["all"] = dict(zip(("correct", "total"), map(int, match.groups())))
    detail = line_for("branch-type:", required=False)
    if detail is not None:
        pattern = r"branch-type:\s+" + r"\s+".join(
            kind + r"=(\d+)/(\d+)\(\d+(?:\.\d+)?%\)" for kind in BRANCH_TYPES[1:])
        match = re.fullmatch(pattern, detail)
        if not match:
            raise ValueError(f"{name}: malformed branch-type statistic: {detail}")
        counts = tuple(map(int, match.groups()))
        for index, kind in enumerate(BRANCH_TYPES[1:]):
            stats[kind] = dict(correct=counts[index * 2], total=counts[index * 2 + 1])
        for field in ("correct", "total"):
            if sum(stats[kind][field] for kind in BRANCH_TYPES[1:]) != stats["all"][field]:
                raise ValueError(f"{name}: branch-type {field} sum differs from branch summary")
    for kind, counts in stats.items():
        if counts is not None and counts["correct"] > counts["total"]:
            raise ValueError(f"{name}: {kind} correct count exceeds total")
    return stats


def branch_rate(counts):
    if counts is None or counts["total"] == 0:
        return "N/A"
    return f"{100 * counts['correct'] / counts['total']:.4f}%"


def branch_totals(rows):
    totals = {}
    for kind in BRANCH_TYPES:
        counts = [row["branches"][kind] for row in rows]
        # Never present a partial aggregate as the whole corpus's accuracy.
        totals[kind] = (None if any(c is None for c in counts) else
                        {field: sum(c[field] for c in counts) for field in ("correct", "total")})
    return totals


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def synchronize(args):
    source = args.source.resolve()
    cases = sorted(p for p in source.iterdir() if p.is_dir() and (p / "program.data").is_file())
    if not cases:
        raise ValueError("empty course corpus")
    manifest = {"protocol": "SW result -> 0x80000000; finish on AXI B handshake", "cases": {}}
    for case in cases:
        hashes = {name: digest(case / name) for name in FILES}
        manifest["cases"][case.name] = hashes
        for target in (ROOT / "data/course-testcases", ROOT / "RISC-V-Simulator-Template/data/course-testcases"):
            directory = target / case.name
            directory.mkdir(parents=True, exist_ok=True)
            for name in FILES:
                if (directory / name).exists() and digest(directory / name) == hashes[name]:
                    continue
                shutil.copyfile(case / name, directory / name)
    for tree in (ROOT, ROOT / "RISC-V-Simulator-Template"):
        (tree / "data/course-testcases/manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    print(f"Synchronized {len(cases)} cases to both software trees (byte-for-byte).")


def simulate(binary, case, timeout, *, collect_branches=False):
    env = dict(os.environ, VERBOSE="clock,branch" if collect_branches else "clock", RESULT_FULL="1")
    env.pop("LEGACY_HALT", None)
    with (case / "program.data").open("rb") as image:
        result = subprocess.run([str(binary.resolve())], stdin=image, capture_output=True,
                                timeout=timeout, env=env, check=True)
    value = int(result.stdout.decode().strip())
    log = result.stderr.decode()
    timing = re.search(r"timing: init=(\d+) core=(\d+) drain=(\d+) total=(\d+)", log)
    ipc = re.search(r"ipc: [\d.]+ retired=(\d+) cycles=(\d+)", log)
    if not timing or not ipc:
        raise ValueError(f"missing timing/IPC output: {case.name}: {log}")
    branches = parse_branches(log, case.name) if collect_branches else None
    return value, tuple(map(int, timing.groups())), int(ipc[1]), branches


def test(args):
    manifest = json.loads((args.data / "manifest.json").read_text())
    columns = ("case", "result", "cycles", "retired", "core", "drain")
    golden = {}
    active = False
    for line in args.golden.read_text().splitlines():
        cells = tuple(c.strip() for c in line.strip().strip("|").split("|"))
        if args.commit_width == 1:
            if len(cells) == 7 and cells[0] == "单提交":
                cells = cells[1:]
                if cells[0] in golden:
                    raise ValueError(f"duplicate single-commit golden: {cells[0]}")
                golden[cells[0]] = dict(zip(columns[1:], map(int, cells[1:])))
            continue
        if cells == columns:
            active = True
        elif active and len(cells) == len(columns) and cells[0] in manifest["cases"]:
            if cells[0] in golden:
                raise ValueError(f"duplicate course golden: {cells[0]}")
            golden[cells[0]] = dict(zip(columns[1:], map(int, cells[1:])))
    if set(golden) != set(manifest["cases"]):
        raise ValueError("course golden must contain exactly the manifest's cases")
    names = sorted(n for n in manifest["cases"] if fnmatch.fnmatchcase(n, args.pattern))
    if not names:
        raise ValueError(f"no cases match {args.pattern!r}")
    rows = []
    columns = ("Case", "Result", "Clock", "Core", "Retired", "IPC", "Br%", "Cond%", "Jal%", "Jalr%")
    widths = (max(len("Case"), *(len(name) for name in names)), 10, 10, 10, 10, 8, 10, 10, 10, 10)

    def print_row(cells):
        print(" | ".join(f"{str(cell):<{width}}" if index == 0 else f"{str(cell):>{width}}"
                         for index, (cell, width) in enumerate(zip(cells, widths))), flush=True)

    print_row(columns)
    print("-+-".join("-" * width for width in widths), flush=True)
    for name in names:
        case = args.data / name
        for filename, expected_hash in manifest["cases"][name].items():
            if digest(case / filename) != expected_hash:
                raise ValueError(f"corpus checksum mismatch: {case / filename}")
        expected = int((case / "expected.txt").read_text())
        instructions = int(json.loads((case / "metrics.json").read_text())["dynamic_instructions"])
        actual, timing, retired, branches = simulate(args.binary, case, args.timeout, collect_branches=True)
        if actual != expected or retired != instructions:
            raise ValueError(f"{name}: result {actual}/{expected}, retired {retired}/{instructions}")
        g = golden[name]
        if (actual, timing, retired) != (g["result"], (1, g["core"], g["drain"], g["cycles"]), g["retired"]):
            raise ValueError(f"{name}: strict cycle/retirement golden mismatch: {(actual, timing, retired)} != {g}")
        if args.peer:
            peer = simulate(args.peer, case, args.timeout)
            if peer[:3] != (actual, timing, retired):
                raise ValueError(f"{name}: trees differ: {(actual, timing, retired)} != {peer}")
        row = dict(case=name, result=actual, init=timing[0], core=timing[1],
                   drain=timing[2], cycles=timing[3], retired=retired, branches=branches)
        rows.append(row)
        print_row((name, actual, timing[3], timing[1], retired, f"{retired / timing[1]:.6f}",
                   *(branch_rate(branches[kind]) for kind in BRANCH_TYPES)))
    if args.report:
        args.report.write_text(json.dumps(rows, indent=2) + "\n")
    print(f"PASS {len(rows)}/{len(names)}" + ("; result/retirement/cycles identical across both trees" if args.peer else ""))
    print(f"Totals: cycles={sum(r['cycles'] for r in rows)} retired={sum(r['retired'] for r in rows)} core={sum(r['core'] for r in rows)}")
    print(f"Geomean IPC: {math.exp(sum(math.log(r['retired'] / r['core']) for r in rows) / len(rows)):.12f}")
    print("Branch accuracy (weighted by counts; correct/total):")
    for kind, counts in branch_totals(rows).items():
        fraction = "unavailable" if counts is None else f"{counts['correct']}/{counts['total']}"
        print(f"  {kind:4s}: {branch_rate(counts):>10s} ({fraction})")
    print("N/A = no samples or simulator does not emit branch-type counts; JALR includes returns.")
    print("Accuracy follows the simulator's resolved next-PC counters, not retirement or direction-only accuracy.")


def smoke(args):
    """Small real-CPU programs for protocol/cache boundaries, not patched images."""
    def add(rd, rs, imm):
        return ((imm & 4095) << 20) | (rs << 15) | (rd << 7) | 0x13

    def lui(rd, upper):
        return (upper << 12) | (rd << 7) | 0x37

    def load(rd, rs, offset=0, kind=2):
        return ((offset & 4095) << 20) | (rs << 15) | (kind << 12) | (rd << 7) | 3

    def store(rs, base, offset=0, kind=2):
        imm = offset & 4095
        return ((imm >> 5) << 25) | (rs << 20) | (base << 15) | (kind << 12) | ((imm & 31) << 7) | 0x23

    def exit_program(body):
        return body + [lui(5, 0x80000), store(10, 5), 0x6f]

    tests = []
    def case(name, body, expected, data=None, retired=None):
        tests.append((name, exit_program(body), expected, data or {}, len(body) + 2 if retired is None else retired))

    case("legal_addi_255", [add(10, 0, 255)], 255)
    case("full_32bit_exit", [lui(10, 0xdeadc), add(10, 10, -273)], 0xdeadbeef)
    for name, upper, offset in (("course_stack", 0x40, -16), ("ram_last_line", 0x10000, -32)):
        case(name, [lui(11, upper), add(11, 11, offset), add(12, 0, 77), store(12, 11), load(10, 11)], 77)
    case("byte_sign_extension", [lui(11, 1), add(12, 0, 128), store(12, 11, 1, 0), load(10, 11, 1, 0)], 0xffffff80)
    body = [lui(11, 1)] + [load(r + 12, 11, r << 6) for r in range(8)] + [add(10, 0, 0)]
    for r in range(8):
        body.append(((r + 12) << 20) | (10 << 15) | (10 << 7) | 0x33)
    case("eight_cold_loads", body, 36, {0x1000 + (r << 6): r + 1 for r in range(8)})
    body = []
    for r in range(8):
        body += [lui(11, 1 + (r << 1)), add(12, 0, r + 1), store(12, 11)]
    body.append(add(10, 0, 0))
    for r in range(8):
        body += [lui(11, 1 + (r << 1)), load(12, 11), (12 << 20) | (10 << 15) | (10 << 7) | 0x33]
    case("dirty_same_set_eviction", body, 36)
    # DIV dependency delays a taken BEQ so the wrong-path exit SW is issued
    # speculatively, but it must never reach the slave before retirement.
    body = [add(10, 0, 7), lui(5, 0x80000), add(6, 0, 3),
            (1 << 25) | (6 << 20) | (6 << 15) | (4 << 12) | (7 << 7) | 0x33,
            (7 << 20) | (7 << 15) | (12 << 7) | 0x63, store(6, 5), add(10, 0, 123)]
    case("wrong_path_exit", body, 7, retired=7)  # five body instructions + LUI/SW
    rows = []
    with tempfile.TemporaryDirectory(prefix="rv32-axi-smoke-", dir="/tmp/opencode") as directory:
        for name, instructions, expected, data, retired in tests:
            path = Path(directory) / name
            path.mkdir()
            memory = {i << 2: word for i, word in enumerate(instructions)} | data
            text = "\n".join(f"@{addr:08x}\n" + " ".join(f"{(word >> (b << 3)) & 255:02x}" for b in range(4))
                             for addr, word in sorted(memory.items())) + "\n"
            (path / "program.data").write_text(text)
            result = simulate(args.binary, path, args.timeout)
            if result[0] != expected or result[2] != retired:
                raise ValueError(f"{name}: {result}, expected result={expected}, retired={retired}")
            if args.peer and simulate(args.peer, path, args.timeout) != result:
                raise ValueError(f"{name}: trees differ")
            rows.append(dict(case=name, result=result[0], timing=result[1], retired=result[2]))
            print(f"PASS {name}: {result[:3]}", flush=True)
    if args.report:
        args.report.write_text(json.dumps(rows, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    sync = commands.add_parser("sync")
    sync.add_argument("--source", type=Path, default=ROOT.parent / "RISC-V-CPU-2026/testcases")
    sync.set_defaults(action=synchronize)
    run = commands.add_parser("test")
    run.add_argument("pattern", nargs="?", default="*")
    run.add_argument("--data", type=Path, default=ROOT / "data/course-testcases")
    run.add_argument("--binary", type=Path, default=Path(os.environ.get("BP_BIN", ROOT / "code")))
    run.add_argument("--peer", type=Path)
    run.add_argument("--timeout", type=float, default=300)
    run.add_argument("--report", type=Path)
    run.add_argument("--golden", type=Path, default=Path(os.environ.get("GOLDEN_MD", ROOT / "docs/benchmarks.md")))
    run.add_argument("--commit-width", type=int, choices=(1, 2), default=2,
                     help="select the matching golden table; does not change the binary's compiled width")
    run.set_defaults(action=test)
    edge = commands.add_parser("smoke")
    edge.add_argument("--binary", type=Path, default=ROOT / "code")
    edge.add_argument("--peer", type=Path)
    edge.add_argument("--timeout", type=float, default=30)
    edge.add_argument("--report", type=Path)
    edge.set_defaults(action=smoke)
    args = parser.parse_args()
    try:
        args.action(args)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()
