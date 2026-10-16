"""Generate C stubs for every symbol that natively compiled firmware objects leave
undefined, using the prototypes in firmware/include and tests/hal_stub.

Functions return oracle values (fuzz_oracle.h): error codes, flags and counts come
from the fuzz input; pointers point at a zeroed buffer; structs are zero.  Data
symbols are defined with their declared type.  Anything the harness object defines
itself is left alone.  Symbols without a prototype in the headers (libc) are
reported and skipped; a wrong guess surfaces as a link error, never silently.
"""
import re
import subprocess
from pathlib import Path

from run_tests import FW, llvm_tool

HEADER_DIRS = (FW / 'include', FW / 'tests' / 'hal_stub')
SKIP_HEADERS = {'iec_nib.h', 'usbd_conf.h', 'stm32h7xx_hal_conf.h', 'ffconf.h'}
SCALARS = ('bool', 'char', 'signed char', 'unsigned char', 'short', 'unsigned short', 'int',
           'unsigned', 'long', 'unsigned long', 'long long', 'unsigned long long', 'float', 'double')


def _strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


def headers():
    out = {}
    for d in HEADER_DIRS:
        for p in sorted(d.glob('*.h')):
            if p.name not in SKIP_HEADERS:
                out[p.name] = _strip_comments(p.read_text(encoding='utf-8', errors='replace'))
    return out


def nm_symbols(objects, flag):
    found = set()
    for o in objects:
        text = subprocess.run([llvm_tool('llvm-nm'), flag, '--format=just-symbols', str(o)],
                              check=True, capture_output=True, text=True).stdout
        found |= {s.strip() for s in text.splitlines() if s.strip()}
    return found


def unresolved(objects):
    return nm_symbols(objects, '-u') - nm_symbols(objects, '--defined-only')


def _return_type(prefix):
    cut = max(prefix.rfind(';'), prefix.rfind('}'), prefix.rfind('{'), prefix.rfind(')'))
    lines = prefix[cut + 1:].splitlines()
    kept = []
    for line in lines:
        if line.lstrip().startswith('#'):
            kept = []
        else:
            kept.append(line)
    ret = ' '.join(' '.join(kept).split())
    ret = re.sub(r'\b(extern|static|inline)\b', '', ret).strip()
    ret = re.sub(r'__attribute__\s*\(\(.*?\)\)', '', ret).strip()
    return ret or None


def find_prototype(sym, hdrs):
    """-> (header, return type, parameter list) or None"""
    pat = re.compile(r'\b' + re.escape(sym) + r'\s*\(((?:[^;{}()]|\([^;{}()]*\))*)\)\s*;', re.S)
    for name, text in hdrs.items():
        for m in pat.finditer(text):
            ret = _return_type(text[:m.start()])
            if ret and not re.search(r'\b(define|typedef|return)\b', ret):
                return name, ret, ' '.join(m.group(1).split()) or 'void'
    return None


def find_data(sym, hdrs):
    pat = re.compile(r'\bextern\s+((?:[^;()=]|\[[^\]]*\])+?)\b' + re.escape(sym) + r'\s*((?:\[[^\]]*\])*)\s*;')
    for name, text in hdrs.items():
        m = pat.search(text)
        if m:
            return name, ' '.join(m.group(1).split()), m.group(2)
    return None


def is_struct(type_name, hdrs):
    t = type_name.replace('const', '').replace('volatile', '').strip()
    if t.startswith(('struct ', 'union ')):
        return True
    closing = re.compile(r'\}\s*' + re.escape(t) + r'\s*;')
    forward = re.compile(r'typedef\s+(?:struct|union)\s+\w+\s+' + re.escape(t) + r'\s*;')
    kind = re.compile(r'typedef\s+(struct|union|enum)\b')
    for text in hdrs.values():
        if forward.search(text):
            return True
        for m in closing.finditer(text):
            kinds = kind.findall(text[:m.start()])
            if kinds:                      # the nearest typedef before the closing brace
                return kinds[-1] != 'enum'
    return False


def stub_function(ret, params):
    if ret == 'void':
        return '{ }'
    if '*' in ret:
        if ret.replace('const', '').replace(' ', '') == 'char*':
            return '{ return fuzz_string(); }'
        return '{ return (%s)fuzz_zero_buffer(); }' % ret
    return None   # scalar or struct: decided by the caller


def generate(objects, out_path, harness_objects=(), extra=None):
    """Write out_path; returns (stubbed symbols, skipped symbols).
    extra: {symbol: (header or None, C definition)} for symbols no header declares
    (linker symbols, externs declared inside a .c file)."""
    hdrs = headers()
    missing = unresolved(list(objects) + list(harness_objects))
    includes, body, skipped = set(), [], []
    for sym in sorted(missing):
        if sym.startswith(('__start_', '__stop_', '__llvm_', '__asan', '__ubsan', '__sanitizer', '__sancov')):
            continue                       # sanitizer / profile runtime, provided by the toolchain
        if extra and sym in extra:
            header, definition = extra[sym]
            if header:
                includes.add(header)
            body.append(definition)
            continue
        proto = find_prototype(sym, hdrs)
        if proto:
            header, ret, params = proto
            includes.add(header)
            code = stub_function(ret, params)
            if code is None:
                if is_struct(ret, hdrs):
                    code = '{ %s fuzz_r_; memset(&fuzz_r_, 0, sizeof fuzz_r_); return fuzz_r_; }' % ret
                else:
                    assoc = ', '.join('%s: (%s)fuzz_u32()' % (s, ret) for s in SCALARS)
                    assoc = assoc.replace('bool: (%s)fuzz_u32()' % ret, 'bool: (%s)fuzz_bit()' % ret)
                    assoc = assoc.replace('int: (%s)fuzz_u32()' % ret, 'int: (%s)fuzz_err()' % ret)
                    code = ('{ %s fuzz_r_ = (%s)0; return _Generic(fuzz_r_, %s, default: fuzz_r_); }'
                            % (ret, ret, assoc))
            body.append('%s %s(%s) %s' % (ret, sym, params, code))
            continue
        data = find_data(sym, hdrs)
        if data:
            header, dtype, dims = data
            includes.add(header)
            body.append('%s %s%s;' % (dtype, sym, dims or ''))
            continue
        skipped.append(sym)
    text = ['/* generated by fuzz_stubs.py: doubles for everything the fuzzed modules call */',
            '#include <stdbool.h>', '#include <stdint.h>', '#include <string.h>',
            '#include "fuzz_oracle.h"']
    text += ['#include "%s"' % h for h in sorted(includes)]
    text += body
    Path(out_path).write_text('\n'.join(text) + '\n', encoding='utf-8')
    return [b.split('(')[0].split()[-1] for b in body], skipped
