"""Worst-case stack depth over call chains and interrupt nesting, from GCC's call graph files.

check_stack.py budgets single frames.  This tool adds the frames along the deepest call chain
below main() and below every interrupt handler (GCC -fcallgraph-info=su writes one *.ci file per
object: nodes with frame sizes, edges for direct calls) and compares

    depth(main) + sum over the N deepest handlers of (depth(handler) + exception frame)

with the stack space the linker leaves (memory_usage.json).  N is the number of preemption
levels: the distinct priorities of HAL_NVIC_SetPriority() in the project sources plus two (the
SysTick level and one fault).  Taking the N deepest handlers regardless of their real level
makes the sum an upper bound for the nesting.

What the result does NOT cover, each reported with its count so the bound is not mistaken for a
proof:
- indirect calls (function pointers): the callee is unknown to the compiler; such a call adds
  nothing to the chain.  The functions containing them are listed.
- functions without a frame size (libgcc, newlib, assembly): each counts EXTERNAL_ALLOWANCE
  bytes, an assumption, not a measurement.
- recursion: a cycle has no static bound; it fails the check unless listed in --allow-cycle.
"""
import argparse
import json
import re
import sys
from pathlib import Path

# Recursions with a bound read from the code: function -> re-entries.  The frame counts
# (re-entries + 1) times.  A recursion that is not listed here fails the check.
BOUNDED_RECURSION = {
    # cancel during spin-up calls ufi_drive_motor(false), which does not wait and cannot recurse
    'ufi_drive_motor': 1,
    # op 24/26 (compare and start) and the track plan (ops 27/28) call the command once more
    # with an inner request: plan resume -> plan create -> start of op 1
    'ufi_analysis_command': 3,
}
EXCEPTION_FRAME = 108          # Cortex-M7 with FPU: 104-byte extended frame + 4 alignment
EXTERNAL_ALLOWANCE = 64         # per call of a function the compiler gave no frame size for
NODE = re.compile(r'^node: \{ title: "([^"]+)" label: "([^"]*)"')
EDGE = re.compile(r'^edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"')
SIZE = re.compile(r'\\n(\d+) bytes \(([^)]*)\)')
PRIO = re.compile(r'HAL_NVIC_SetPriority\(\s*\w+\s*,\s*(\d+)\s*,')


def parse(files):
    """-> sizes {title: bytes}, calls {title: set(titles)}, unbounded [titles]."""
    sizes, calls, unbounded = {}, {}, []
    for path in files:
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            m = NODE.match(line)
            if m:
                s = SIZE.search(m.group(2))
                if s:
                    sizes[m.group(1)] = int(s.group(1))
                    if s.group(2) not in ('static', 'dynamic,bounded'):
                        unbounded.append(m.group(1))
                continue
            m = EDGE.match(line)
            if m:
                calls.setdefault(m.group(1), set()).add(m.group(2))
    # GCC titles a weak definition "path:name" like a static one, while callers in other objects
    # use the bare name.  A bare target without a definition of its own is every such definition
    # (the deepest one counts): a strong override would carry the bare title and win here.
    by_name = {}
    for title in sizes:
        if ':' in title:
            by_name.setdefault(title.rsplit(':', 1)[1], set()).add(title)
    for src, targets in calls.items():
        for t in [t for t in targets if t not in sizes and t in by_name and not src.endswith(':' + t)]:
            targets.discard(t)
            targets.update(by_name[t])
    return sizes, calls, unbounded


def cycles(calls):
    """Functions that can reach themselves (Tarjan, iterative)."""
    index, low, on, stack, out, n = {}, {}, set(), [], set(), 0
    for root in list(calls):
        if root in index:
            continue
        work = [(root, iter(sorted(calls.get(root, ()))))]
        index[root] = low[root] = n
        n += 1
        stack.append(root)
        on.add(root)
        while work:
            v, it = work[-1]
            for w in it:
                if w not in index:
                    index[w] = low[w] = n
                    n += 1
                    stack.append(w)
                    on.add(w)
                    work.append((w, iter(sorted(calls.get(w, ())))))
                    break
                if w in on:
                    low[v] = min(low[v], index[w])
            else:
                work.pop()
                if work:
                    low[work[-1][0]] = min(low[work[-1][0]], low[v])
                if low[v] == index[v]:
                    comp = []
                    while True:
                        w = stack.pop()
                        on.discard(w)
                        comp.append(w)
                        if w == v:
                            break
                    if len(comp) > 1 or v in calls.get(v, ()):
                        out.update(comp)
    return out


def depths(sizes, calls, cyclic):
    """-> depth {title: bytes}, chain {title: next title on the deepest path}."""
    depth, nxt = {}, {}

    def frame(f):
        if f == '__indirect_call':
            return 0
        return sizes.get(f, EXTERNAL_ALLOWANCE) * (1 + BOUNDED_RECURSION.get(short(f), 0))

    order, seen = [], set()
    for root in set(calls) | set(sizes):
        if root in seen:
            continue
        work = [(root, iter(sorted(calls.get(root, ()))))]
        seen.add(root)
        while work:
            v, it = work[-1]
            for w in it:
                if w not in seen and w not in cyclic:
                    seen.add(w)
                    work.append((w, iter(sorted(calls.get(w, ())))))
                    break
            else:
                work.pop()
                order.append(v)
    for f in order:                                     # callees before callers
        best, via = 0, None
        for c in calls.get(f, ()):
            if c == f:                                  # bounded self-recursion: counted in frame()
                continue
            d = depth.get(c, frame(c))
            if d > best:
                best, via = d, c
        depth[f] = frame(f) + best
        nxt[f] = via
    return depth, nxt


def chain(f, nxt):
    out = []
    while f is not None and len(out) < 64:
        out.append(f)
        f = nxt.get(f)
    return out


def short(title):
    return title.rsplit('/', 1)[-1]


def analyse(files, stack_space, levels, allow_cycle=()):
    sizes, calls, unbounded = parse(files)
    if 'main' not in sizes:
        raise ValueError('No call graph for main(); rebuild with -fcallgraph-info=su')
    cyclic = {f for f in cycles(calls) if short(f) not in BOUNDED_RECURSION and short(f) not in allow_cycle}
    depth, nxt = depths(sizes, calls, cyclic)
    handlers = sorted((f for f in sizes if re.search(r'(_IRQHandler|_Handler)$', f)),
                      key=lambda f: depth[f], reverse=True)
    nested = handlers[:levels]
    total = depth['main'] + sum(depth[h] + EXCEPTION_FRAME for h in nested)
    reach = set()
    todo = ['main', *handlers]
    while todo:
        f = todo.pop()
        if f not in reach:
            reach.add(f)
            todo.extend(calls.get(f, ()))
    report = dict(
        stack_space=stack_space, worst_case=total, margin=stack_space - total,
        main=dict(bytes=depth['main'], chain=[short(f) for f in chain('main', nxt)]),
        preemption_levels=levels, exception_frame=EXCEPTION_FRAME,
        handlers=[dict(function=short(h), bytes=depth[h], chain=[short(f) for f in chain(h, nxt)]) for h in nested],
        handlers_total=len(handlers),
        indirect_callers=sorted(short(f) for f in reach if '__indirect_call' in calls.get(f, ())),
        external_allowance=EXTERNAL_ALLOWANCE,
        externals=sorted(short(f) for f in reach if f not in sizes and f != '__indirect_call'),
        recursion=sorted(short(f) for f in cyclic & reach),
        bounded_recursion=BOUNDED_RECURSION,
        unbounded=sorted(short(f) for f in unbounded),
        scope='Upper bound over direct calls only: indirect calls add nothing, functions without a frame size '
              'count the allowance. Not measured on the board; item 13 stack_free of SELFTEST is the measurement.')
    errors = []
    if report['recursion']:
        errors.append('recursion without a static bound: ' + ', '.join(report['recursion'][:8]))
    if report['unbounded']:
        errors.append('unbounded frame: ' + ', '.join(report['unbounded'][:8]))
    if total > stack_space:
        errors.append(f'worst case {total} B exceeds the stack space {stack_space} B')
    return report, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('build', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--allow-cycle', action='append', default=[])
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    files = sorted((args.build / 'CMakeFiles/ufi_firmware.elf.dir').rglob('*.ci'))
    if not files:
        raise SystemExit('No *.ci call graph files; rebuild the firmware')
    stack_space = json.loads((args.build / 'memory_usage.json').read_text(encoding='utf-8'))['stack_space']
    prios = {int(p) for src in (root / 'src').glob('*.c') for p in PRIO.findall(src.read_text(encoding='utf-8', errors='replace'))}
    report, errors = analyse(files, stack_space, len(prios) + 2, args.allow_cycle)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"Call chains: main {report['main']['bytes']} B + {report['preemption_levels']} nested handlers = "
          f"{report['worst_case']} B of {stack_space} B (margin {report['margin']} B); not covered: "
          f"{len(report['indirect_callers'])} functions with indirect calls, {len(report['externals'])} externals at "
          f"{EXTERNAL_ALLOWANCE} B each.")
    if errors:
        raise SystemExit('Call graph check failed: ' + '; '.join(errors))


if __name__ == '__main__':
    sys.exit(main())
