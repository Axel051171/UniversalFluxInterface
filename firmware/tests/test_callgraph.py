"""tools/check_callgraph.py on synthetic call graphs: chain sums, interrupt nesting, weak
definitions, externals, recursion (unbounded fails, listed one counts its re-entries) and the
budget failure.  The check must be able to fail, so every failure path is provoked here."""
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import check_callgraph as cg  # noqa: E402


def node(title, size=None, kind='static'):
    name = title.rsplit(':', 1)[-1]
    if size is None:
        return 'node: { title: "%s" label: "%s\\nx.h:1:1" shape : ellipse }' % (title, name)
    return 'node: { title: "%s" label: "%s\\nx.c:1:1\\n%d bytes (%s)" }' % (title, name, size, kind)


def edge(a, b):
    return 'edge: { sourcename: "%s" targetname: "%s" label: "x.c:2:2" }' % (a, b)


def graph(*lines):
    return 'graph: { title: "x.c"\n' + '\n'.join(lines) + '\n}\n'


def run(files, space=1000, levels=2):
    with tempfile.TemporaryDirectory() as d:
        paths = []
        for i, text in enumerate(files):
            p = Path(d) / ('f%d.c.ci' % i)
            p.write_text(text, encoding='utf-8')
            paths.append(p)
        return cg.analyse(paths, space, levels)


def main():
    # deepest chain, not the longest: main(40) -> a(100) -> leaf(8) beats main -> b(20) -> c(20) -> d(20)
    base = [graph(node('main', 40), node('a', 100), node('leaf', 8), node('b', 20), node('c', 20), node('d', 20),
                  edge('main', 'a'), edge('a', 'leaf'), edge('main', 'b'), edge('b', 'c'), edge('c', 'd'))]
    r, e = run(base)
    assert not e and r['main']['bytes'] == 148 and r['main']['chain'] == ['main', 'a', 'leaf'] and r['worst_case'] == 148

    # interrupt nesting: the `levels` deepest handlers, each with the exception frame
    irq = base + [graph(node('A_IRQHandler', 30), node('SysTick_Handler', 10), node('B_IRQHandler', 50),
                        node('deep', 200), edge('B_IRQHandler', 'deep'))]
    r, e = run(irq)
    assert not e and [h['function'] for h in r['handlers']] == ['B_IRQHandler', 'A_IRQHandler'] and r['handlers_total'] == 3
    assert r['worst_case'] == 148 + (250 + cg.EXCEPTION_FRAME) + (30 + cg.EXCEPTION_FRAME)
    assert run(irq, levels=3)[0]['worst_case'] == r['worst_case'] + 10 + cg.EXCEPTION_FRAME
    r, e = run(irq, space=600)
    assert e and 'exceeds the stack space 600' in e[0] and r['margin'] < 0

    # a weak definition is titled "path:name"; the bare call must reach it, the deepest one counts
    weak = [graph(node('main', 40), node('HAL_X'), edge('main', 'HAL_X')),
            graph(node('C:/hal/hal.c:HAL_X', 300), node('C:/hal/hal.c:helper', 60),
                  edge('C:/hal/hal.c:HAL_X', 'C:/hal/hal.c:helper'))]
    r, e = run(weak)
    assert not e and r['main']['bytes'] == 400 and r['externals'] == []
    # a strong override with the bare title wins over the weak one
    strong = weak + [graph(node('HAL_X', 16))]
    assert run(strong)[0]['main']['bytes'] == 56

    # functions without a frame size count the allowance and are listed; indirect calls add nothing but are listed
    ext = [graph(node('main', 40), node('memcpy'), node('__indirect_call'), edge('main', 'memcpy'),
                 edge('main', '__indirect_call'))]
    r, e = run(ext)
    assert not e and r['main']['bytes'] == 40 + cg.EXTERNAL_ALLOWANCE and r['externals'] == ['memcpy']
    assert r['indirect_callers'] == ['main']

    # recursion: direct and mutual fail; reachable only
    rec = [graph(node('main', 40), node('r', 10), edge('main', 'r'), edge('r', 'r'))]
    r, e = run(rec)
    assert e and 'recursion' in e[0] and r['recursion'] == ['r']
    mutual = [graph(node('main', 40), node('p', 10), node('q', 10), edge('main', 'p'), edge('p', 'q'), edge('q', 'p'))]
    assert run(mutual)[0]['recursion'] == ['p', 'q'] and run(mutual)[1]
    unreachable = [graph(node('main', 40), node('r', 10), edge('r', 'r'))]
    assert run(unreachable) [1] == [] and run(unreachable)[0]['recursion'] == []
    # a listed recursion counts its frame (re-entries + 1) times and passes
    name, again = 'ufi_drive_motor', cg.BOUNDED_RECURSION['ufi_drive_motor']
    listed = [graph(node('main', 40), node(name, 24), node('leaf', 8), edge('main', name), edge(name, name), edge(name, 'leaf'))]
    r, e = run(listed)
    assert not e and r['main']['bytes'] == 40 + 24 * (again + 1) + 8 and r['main']['chain'] == ['main', name, 'leaf']

    # an unbounded frame (alloca / VLA) fails
    r, e = run([graph(node('main', 40), node('v', 64, 'dynamic'), edge('main', 'v'))])
    assert e and 'unbounded frame: v' in e[0]
    # no main: the graph files are missing or stale
    try:
        run([graph(node('a', 1))])
    except ValueError as err:
        assert 'main' in str(err)
    else:
        raise AssertionError('missing main accepted')
    print('call graph check: chain sum, nesting, weak definitions, externals, recursion, unbounded frame, budget passed')


if __name__ == '__main__':
    main()
