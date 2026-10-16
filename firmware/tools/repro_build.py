"""Two fresh ARM builds with locked tools, fixed date and an auditable hash manifest.

Uses existing pinned dependencies; never deletes a previous build or publishes files.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
LOCK={'gcc':'14.2.1','cmake':'4.4.3','ninja':'1.13.2','cube':'FW.H7.1.11.0'}

def output(*cmd):return subprocess.check_output(cmd,text=True).strip()
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--cube',type=Path,required=True)
    parser.add_argument('--epoch',type=int,default=1792108800)
    args=parser.parse_args()
    versions={'gcc':output('arm-none-eabi-gcc','-dumpfullversion'),
              'cmake':output('cmake','--version').splitlines()[0].split()[-1],
              'ninja':output('ninja','--version')}
    for tool,value in versions.items():
        if value!=LOCK[tool]:raise SystemExit(f'{tool}: expected {LOCK[tool]}, found {value}')
    cube=args.cube.resolve()
    if LOCK['cube'] not in (cube/'package.xml').read_text():raise SystemExit('STM32Cube package version mismatch')
    env=os.environ.copy();env['SOURCE_DATE_EPOCH']=str(args.epoch)
    base=ROOT/'firmware/tests/build';base.mkdir(exist_ok=True)
    run=Path(tempfile.mkdtemp(prefix='repro-',dir=base))
    manifest=dict(lock=LOCK,versions=versions,epoch=args.epoch,builds=[],sources={})
    sources=[ROOT/'firmware/CMakeLists.txt',ROOT/'firmware/STM32H723ZGTX_FLASH.ld']
    for folder in ('src','include'):
        sources.extend(p for p in (ROOT/'firmware'/folder).rglob('*') if p.is_file())
    for p in sorted(sources):manifest['sources'][p.relative_to(ROOT).as_posix()]=digest(p)
    source_digest=hashlib.sha256(json.dumps(manifest['sources'],sort_keys=True).encode()).hexdigest()
    manifest['source_sha256']=source_digest
    for index in range(2):
        dest=run/str(index)
        with (run/f'build{index}.log').open('w',encoding='utf-8') as log:
            subprocess.run(['cmake','-S',str(ROOT/'firmware'),'-B',str(dest),'-G','Ninja',
                            f'-DSTM32CUBE_PATH={cube}',f'-DUFI_GIT_REV=src-{source_digest[:12]}'],
                           env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
            subprocess.run(['cmake','--build',str(dest)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        manifest['builds'].append(dict(directory=str(dest),bin_sha256=digest(dest/'ufi_firmware.bin'),
                                      memory=json.loads((dest/'memory_usage.json').read_text())))
    # Include the actually consumed headers and sources, including the vendor package.
    deps=output('ninja','-C',str(run/'0'),'-t','deps').splitlines()
    vendor={}
    for line in deps:
        if not line.startswith('    '):continue
        p=Path(line.strip())
        if not p.is_absolute():p=run/'0'/p
        p=p.resolve()
        if p.is_file() and cube in p.parents:vendor[p.relative_to(cube).as_posix()]=digest(p)
    manifest['vendor_files']=vendor
    manifest['identical_bin']=manifest['builds'][0]['bin_sha256']==manifest['builds'][1]['bin_sha256']
    (run/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    if not manifest['identical_bin']:raise SystemExit(f'Non-reproducible firmware: {run}')
    print(f'Two fresh builds are byte-identical: {manifest["builds"][0]["bin_sha256"]}\nManifest: {run/"manifest.json"}')

if __name__=='__main__':main()
