"""Budget GCC's measured project frames; this is not a call-chain/IRQ stack proof."""
import argparse
import json
from pathlib import Path


def validate(rows,limit=2048):
    if not rows:raise ValueError('No project stack-usage records')
    for row in rows:
        if row['kind'] not in ('static','dynamic,bounded'):
            raise ValueError('Unbounded stack use: '+row['function'])
        if row['bytes']>limit:
            raise ValueError(f"Stack frame exceeds {limit} B: {row['function']} ({row['bytes']} B)")
    return sorted(rows,key=lambda row:row['bytes'],reverse=True)


def inspect(build):
    root=Path(__file__).resolve().parents[1]
    files=list((build/'CMakeFiles/ufi_firmware.elf.dir/src').glob('*.su'))
    expected={p.name+'.su' for p in (root/'src').glob('*.c')}
    if {p.name for p in files}!=expected:
        raise ValueError('Missing/stale project stack-usage files; rebuild the firmware')
    rows=[]
    for path in files:
        for line in path.read_text(encoding='utf-8').splitlines():
            location,size,kind=line.split('\t')
            source,line,column,function=location.rsplit(':',3)
            rows.append(dict(source=Path(source).name,line=int(line),function=function,bytes=int(size),kind=kind))
    return rows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build',type=Path)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    rows=validate(inspect(args.build))
    report=dict(limit=2048,maximum=rows[0]['bytes'],functions=len(rows),frames=rows,
                scope='Individual project frames; excludes library frames, call-chain sums and interrupt nesting.')
    if args.report:args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f"Project stack frames OK: max {rows[0]['bytes']} B / 2048 B ({rows[0]['function']}); "
          'call chains and IRQ nesting require separate validation.')


if __name__=='__main__':
    main()
